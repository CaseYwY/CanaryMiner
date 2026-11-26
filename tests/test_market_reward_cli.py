"""Production reward command with saved status and a socket-free installed helper."""
from concurrent.futures import ThreadPoolExecutor
import ctypes
from datetime import datetime, timezone
from hashlib import sha256
import json
import os
from pathlib import Path
import random
import shutil
import signal
import subprocess
import sys
import tempfile
import time

ALLOW = "--allow-public-reward"
TIP_HASH = "ab" * 32
MAX_TIP = (1 << 31) - 2
ARCHIVE_HEADER = b"CMR1 PearlMainnetTip\n"


def run(cli, cwd, *args):
    result = subprocess.run([cli, "market-reward", *args], cwd=cwd, capture_output=True, text=True, timeout=10)
    assert "FORBIDDEN_DEVICE_SETUP" not in result.stdout + result.stderr, result
    return result


def status(height, when):
    utc = datetime.fromtimestamp(when, timezone.utc).strftime("%Y-%m-%dT%H:%M:%S")
    return {
        "blockbook": {"coin": "Pearl", "network": "PRL", "decimals": 8, "inSync": True,
                      "initialSync": False, "syncMode": True, "bestHeight": height,
                      "lastBlockTime": utc + ".169902767Z"},
        "backend": {"chain": "mainnet", "blocks": height, "headers": height, "bestBlockHash": TIP_HASH},
    }


def expected_subsidy(tip):
    h = tip + 1
    decay = 4 * 365 * 24 * 60 * 60 // 194
    grains = 2_100_000_000 * 100_000_000 * decay // ((h + decay) * (h + decay - 1))
    return f"{grains // 100_000_000}.{grains % 100_000_000:08} PRL"


def assert_reward(result, height):
    assert result.returncode == 0 and not result.stderr, result
    assert f"Indexed height: {height}\n" in result.stdout, result
    assert f"Backend tip hash: {TIP_HASH}\n" in result.stdout, result
    assert f"Next block height: {height + 1}\n" in result.stdout, result
    assert f"Next block subsidy: {expected_subsidy(height)}" in result.stdout, result
    assert "scheduled consensus subsidy, excluding fees" in result.stdout, result


def with_checksum(payload):
    return payload + sha256(payload).hexdigest().encode("ascii") + b"\n"


def offline_archive(cli, root, cutoff):
    file = root / "\u00adcapture 主网.cmr"
    body = json.dumps(status(42, cutoff), indent=2).encode("ascii") + b"\n"
    report = (
        "Pearl mainnet subsidy [historical Blockbook capture]\n"
        f"Indexed height: 42\nBackend tip hash: {TIP_HASH}\nIndexer update UTC: {cutoff}\n"
        f"Observation UTC: {cutoff}\nNext block height: 43\n"
        f"Next block subsidy: {expected_subsidy(42)} (scheduled consensus subsidy, excluding fees)\n"
        f"Request start UTC: {cutoff - 1}\nCapture completed UTC: {cutoff + 1}\n"
    ).encode("ascii")

    def encode(sent, observed, received, raw, shown):
        return with_checksum(ARCHIVE_HEADER + f"{sent}\n{observed}\n{received}\n{len(raw)}\n".encode("ascii") +
                             raw + f"{len(shown)}\n".encode("ascii") + shown)

    original = encode(cutoff - 1, cutoff, cutoff + 1, body, report)
    file.write_bytes(original)
    result = run(cli, root, "--open-review", file.name)
    assert_reward(result, 42)
    assert result.stdout.encode("ascii") == report and file.read_bytes() == original, result
    reject = (
        (original[:-1], "archive unavailable or invalid"),
        (original + b"extra", "archive unavailable or invalid"),
        (original.replace(b"PearlMainnetTip", b"ForeignFeed"), "archive unavailable or invalid"),
        (with_checksum(original[:-65].replace(b"CMR1 ", b"CMR2 ", 1)), "archive unavailable or invalid"),
        (with_checksum(original[:-65].replace(b"\n" + str(cutoff - 1).encode(), b"\n0" + str(cutoff - 1).encode(), 1)),
         "archive unavailable or invalid"),
        (encode(cutoff + 1, cutoff, cutoff + 1, body, report), "observation times"),
        (encode(cutoff - 1, cutoff, cutoff - 1, body, report), "observation times"),
        (encode(cutoff - 1, cutoff, cutoff + 3600, body, report), "observation times"),
        (encode(cutoff - 1, cutoff, cutoff + 1, body.replace(b'"mainnet"', b'"testnet"'), report), "invalid or stale"),
        (encode(cutoff - 1, cutoff, cutoff + 1, body, report.replace(b"height: 43", b"height: 44")),
         "differs from the recalculated report"),
        (encode(cutoff - 1, cutoff, cutoff + 1, body, b"X" * 1025), "archive unavailable or invalid"),
    )
    for data, error in reject:
        file.write_bytes(data)
        result = run(cli, root, "--open-review", file.name)
        assert result.returncode == 3 and not result.stdout and error in result.stderr, result
        assert file.read_bytes() == data
    file.unlink()


def offline(cli, root):
    cutoff = int(time.time()) - 30
    file = root / "saved index \u4e3b\u7f51.json"
    args = ("--file", file.name, "--as-of", str(cutoff))
    rng = random.Random(650226)
    heights = (0, 1, 148_569, 650_225, 650_226, 650_227, MAX_TIP, *(rng.randrange(MAX_TIP) for _ in range(32)))
    for height in heights:
        file.write_text(json.dumps(status(height, cutoff - 1800)), encoding="utf-8")
        before = file.read_bytes()
        result = run(cli, root, *args)
        assert_reward(result, height)
        assert f"Indexer update UTC: {cutoff - 1800}" in result.stdout
        assert f"Declared cutoff UTC: {cutoff}" in result.stdout
        assert "saved Blockbook status, user-declared cutoff" in result.stdout
        assert file.read_bytes() == before

    precise = status(42, cutoff)
    whole_second = precise["blockbook"]["lastBlockTime"].split(".", 1)[0]
    precise["blockbook"]["lastBlockTime"] = whole_second + ".000000000Z"
    file.write_text(json.dumps(precise), encoding="utf-8")
    assert_reward(run(cli, root, *args), 42)
    precise["blockbook"]["lastBlockTime"] = whole_second + ".000000001Z"
    file.write_text(json.dumps(precise), encoding="utf-8")
    after_cutoff = run(cli, root, *args)
    assert after_cutoff.returncode == 3 and not after_cutoff.stdout and "future" in after_cutoff.stderr

    for group, field, value, error in (
        ("blockbook", "coin", "Bitcoin", "invalid Pearl"),
        ("blockbook", "network", "TPRL", "invalid Pearl"),
        ("backend", "chain", "testnet", "invalid Pearl"),
        ("blockbook", "decimals", 12, "invalid Pearl"),
        ("blockbook", "bestHeight", MAX_TIP + 1, "invalid Pearl"),
        ("blockbook", "bestHeight", 42.0, "invalid Pearl"),
        ("blockbook", "initialSync", True, "not synchronized"),
        ("blockbook", "inSync", False, "not synchronized"),
        ("blockbook", "inSync", "true", "invalid Pearl"),
        ("backend", "blocks", 41, "not synchronized"),
        ("backend", "headers", 43, "not synchronized"),
        ("backend", "bestBlockHash", "0" * 64, "invalid Pearl"),
        ("backend", "bestBlockHash", "ab" * 31 + "\n!", "invalid Pearl"),
        ("blockbook", "lastBlockTime", "2025-02-29T00:00:00Z", "index time is invalid"),
        ("blockbook", "lastBlockTime", "2026-03-01T00:00:00.Z", "index time is invalid"),
        ("blockbook", "lastBlockTime", "2026-03-01T00:00:60Z", "index time is invalid"),
        ("blockbook", "lastBlockTime", "2026-03-01T00:00:00+00:00", "index time is invalid"),
        ("blockbook", "lastBlockTime", status(42, cutoff + 1)["blockbook"]["lastBlockTime"], "index time is invalid"),
        ("blockbook", "lastBlockTime", status(42, cutoff - 1801)["blockbook"]["lastBlockTime"], "over 1800 seconds"),
    ):
        body = status(42, cutoff - 5)
        body[group][field] = value
        file.write_text(json.dumps(body), encoding="utf-8")
        result = run(cli, root, *args)
        assert result.returncode == 3 and error in result.stderr and not result.stdout, result

    for data in ("", "[1, 2]", '{"blockbook":{},"blockbook":{}}', " " * 16_385):
        file.write_text(data, encoding="utf-8")
        result = run(cli, root, *args)
        assert result.returncode == 3 and not result.stdout, result

    file.write_text(json.dumps(status(42, cutoff)), encoding="utf-8")
    future = run(cli, root, "--file", file.name, "--as-of", str(cutoff + 3600))
    assert future.returncode == 3 and "future" in future.stderr and not future.stdout
    for args in (("--file", file.name), ("--file", file.name, "--as-of", "0" + str(cutoff)),
                 ("--file", file.name, "--as-of", str(cutoff), ALLOW),
                 ("--file", file.name, "--as-o\uff46", str(cutoff)),
                 ("--fil\uff45", file.name, "--as-of", str(cutoff))):
        result = run(cli, root, *args)
        assert result.returncode == 2 and not result.stdout, result
    assert {x.name for x in root.iterdir()} == {file.name}
    offline_archive(cli, root, cutoff)
    print("Pearl subsidy review checks exact consensus floor, historical cutoff, identity, sync and input limits")


def public(cli_path, fixture, enabled, root):
    installed = root / "installed"
    installed.mkdir()
    cli = Path(shutil.copy2(cli_path, installed / cli_path.name))
    name = "canary_public_json_worker" + (".exe" if os.name == "nt" else "")
    shutil.copy2(fixture, installed / name)
    requests = installed / "reward-requests"
    resolver_requests = installed / "resolver-requests"
    body = installed / "reward-response.json"
    scenario = installed / "reward-scenario"
    config = root / "config.json"
    config.write_text("private-sentinel", encoding="ascii")
    archive = root / "review.cmr"
    temporary_archive = archive.with_suffix(".cmr.tmp")
    resolver = "192.168.50.53"
    for args in ((), ("--dns-server", resolver), (ALLOW, ALLOW), (ALLOW, "--dns-server"),
                 (ALLOW, "--dns-server", "host.example"),
                 (ALLOW, "--dns-server", f"{resolver}:53"),
                 ("--allow-public-rewar\uff44",), (ALLOW, "--dn\uff53-server", resolver),
                 ("--save-review", archive.name), (ALLOW, "--save-review", "../escape.cmr"),
                 (ALLOW, "--save-review", "review.cmh"), (ALLOW, "--save-review", "NUL.cmr"),
                 (ALLOW, "--save-review", archive.name, "--save-review", archive.name),
                 (ALLOW, "--save-revie\uff57", archive.name), (ALLOW, "--save-review", "revie\uff57.cmr"),
                 (ALLOW, "--sav\u00f0-review", archive.name), (ALLOW, "\u00ad-save-review", archive.name),
                 ("--open-review", archive.name, ALLOW), ("--open-review",), ("--open-review", "--help"),
                 ("--h\u00f0lp",)):
        result = run(cli, root, *args)
        assert result.returncode == 2 and not result.stdout and not requests.exists(), result
    for flag in ("-h", "--help"):
        top_help = subprocess.run([cli, flag], cwd=root, capture_output=True, text=True, timeout=10)
        assert top_help.returncode == 0 and "canary-miner market-reward" in top_help.stdout, top_help
        assert "FORBIDDEN_DEVICE_SETUP" not in top_help.stdout + top_help.stderr, top_help
    help_result = run(cli, root, "--help")
    assert help_result.returncode == 0 and "--as-of UTC_SECONDS" in help_result.stdout
    assert "--allow-public-reward" in help_result.stdout
    assert not requests.exists()
    body.write_text(json.dumps(status(148_569, int(time.time()) - 1)), encoding="utf-8")
    default = run(cli, root, ALLOW)
    if not enabled:
        for attempt in (default, run(cli, root, ALLOW, "--dns-server", resolver),
                        run(cli, root, ALLOW, "--save-review", archive.name)):
            assert attempt.returncode == 3 and "HTTPS unavailable in this build" in attempt.stderr, attempt
        assert not requests.exists() and not resolver_requests.exists() and not archive.exists()
        print("Default-off reward command started no helper")
        return 0
    if sys.platform == "linux" and not requests.exists() and "backend unavailable" in default.stderr:
        libc = os.confstr("CS_GNU_LIBC_VERSION")
        assert libc and tuple(map(int, libc.split()[1].split(".")[:2])) < (2, 40), default
        return 77
    assert_reward(default, 148_569)
    assert "live Blockbook indexer, locally observed" in default.stdout
    assert requests.read_text().splitlines() == ["6"] and not resolver_requests.exists()
    requests.unlink()
    result = run(cli, root, ALLOW, "--dns-server", resolver)
    assert_reward(result, 148_569)
    assert resolver not in result.stdout
    assert requests.read_text().splitlines() == ["6"] and resolver_requests.read_text().splitlines() == [resolver]
    requests.unlink()
    resolver_requests.unlink()
    original_response = json.dumps(status(148_569, int(time.time()) - 1), indent=2).encode("ascii") + b"\n"
    body.write_bytes(original_response)
    saved = run(cli, root, ALLOW, "--save-review", archive.name, "--dns-server", resolver)
    assert_reward(saved, 148_569)
    assert "historical Blockbook capture" in saved.stdout and resolver not in saved.stdout, saved
    assert requests.read_text().splitlines() == ["6"] and resolver_requests.read_text().splitlines() == [resolver]
    requests.unlink()
    resolver_requests.unlink()
    original_archive = archive.read_bytes()
    assert original_archive.startswith(ARCHIVE_HEADER) and original_response in original_archive
    assert with_checksum(original_archive[:-65]) == original_archive
    reopened = run(cli, root, "--open-review", archive.name)
    assert reopened.returncode == 0 and reopened.stdout == saved.stdout and not reopened.stderr, reopened
    assert not requests.exists() and not resolver_requests.exists() and archive.read_bytes() == original_archive
    existing = run(cli, root, ALLOW, "--save-review", archive.name)
    assert existing.returncode == 3 and not existing.stdout and "existing archive was preserved" in existing.stderr, existing
    assert archive.read_bytes() == original_archive and not temporary_archive.exists()
    assert requests.read_text().splitlines() == ["6"]
    requests.unlink()
    archive.unlink()
    temporary_archive.write_bytes(b"existing temporary")
    collision = run(cli, root, ALLOW, "--dns-server", resolver, "--save-review", archive.name)
    assert collision.returncode == 3 and not collision.stdout and not archive.exists(), collision
    assert temporary_archive.read_bytes() == b"existing temporary"
    temporary_archive.unlink()
    requests.unlink()
    resolver_requests.unlink()
    with tempfile.TemporaryDirectory(dir=root) as other_dir:
        other = Path(other_dir)
        rival = Path(shutil.copy2(cli_path, other / cli_path.name))
        shutil.copy2(fixture, other / name)
        (other / body.name).write_bytes(original_response)
        with ThreadPoolExecutor(max_workers=2) as pool:
            submissions = [pool.submit(run, contender, root, ALLOW, "--save-review", archive.name)
                           for contender in (cli, rival)]
            simultaneous = [submission.result() for submission in submissions]
        assert (other / requests.name).read_text().splitlines() == ["6"]
    assert sorted(result.returncode for result in simultaneous) == [0, 3], simultaneous
    winner = next(result for result in simultaneous if result.returncode == 0)
    loser = next(result for result in simultaneous if result.returncode == 3)
    assert not loser.stdout and "existing archive was preserved" in loser.stderr, loser
    assert run(cli, root, "--open-review", archive.name).stdout == winner.stdout
    assert archive.is_file() and not temporary_archive.exists() and requests.read_text().splitlines() == ["6"]
    archive.unlink()
    requests.unlink()
    for mode, expected in (("future", "invalid response observation time"), ("timeout", "deadline exceeded")):
        scenario.write_text(mode, encoding="ascii")
        result = run(cli, root, ALLOW, "--save-review", archive.name)
        assert result.returncode == 3 and expected in result.stderr and not result.stdout, result
        assert not archive.exists() and not temporary_archive.exists()
        assert requests.read_text().splitlines() == ["6"] and not resolver_requests.exists()
        requests.unlink()
    can_signal = os.name != "nt" or ctypes.windll.kernel32.GetConsoleCP() != 0
    if can_signal:
        flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
        scenario.write_text("slow", encoding="ascii")
        with subprocess.Popen([cli, "market-reward", ALLOW, "--save-review", archive.name], cwd=root, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, creationflags=flags, text=True) as child:
            try:
                deadline = time.monotonic() + 4
                while child.poll() is None and time.monotonic() < deadline:
                    if requests.exists() and requests.read_text().splitlines() == ["6"]:
                        break
                    time.sleep(0.02)
                else:
                    raise AssertionError("reward helper did not start")
                child.send_signal(signal.CTRL_BREAK_EVENT if os.name == "nt" else signal.SIGINT)
                out, error = child.communicate(timeout=4)
                assert child.returncode == 130 and not out and "sample cancelled" in error
                assert not archive.exists() and not temporary_archive.exists()
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait(timeout=3)
        requests.unlink()
    assert config.read_text() == "private-sentinel"
    assert {x.name for x in root.iterdir()} == {config.name, installed.name}
    print("Public reward checks consent, original bytes, exclusive capture, replay, clock bound and cancellation")
    return 0


if __name__ == "__main__":
    cli = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="canary-market-reward-") as temporary:
        root = Path(temporary)
        if sys.argv[2] == "--offline":
            offline(cli, root)
        else:
            sys.exit(public(cli, Path(sys.argv[2]).resolve(), sys.argv[3] == "1", root))
