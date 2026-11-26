"""Production reward command with saved status and a socket-free installed helper."""
from datetime import datetime, timezone
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
    resolver = "192.168.50.53"
    for args in ((), ("--dns-server", resolver), (ALLOW, ALLOW), (ALLOW, "--dns-server"),
                 (ALLOW, "--dns-server", "host.example"),
                 (ALLOW, "--dns-server", f"{resolver}:53"),
                 ("--allow-public-rewar\uff44",), (ALLOW, "--dn\uff53-server", resolver)):
        result = run(cli, root, *args)
        assert result.returncode == 2 and not result.stdout and not requests.exists(), result
    help_result = run(cli, root, "--help")
    assert help_result.returncode == 0 and "--as-of UTC_SECONDS" in help_result.stdout
    assert "--allow-public-reward" in help_result.stdout
    assert not requests.exists()
    body.write_text(json.dumps(status(148_569, int(time.time()) - 1)), encoding="utf-8")
    default = run(cli, root, ALLOW)
    if not enabled:
        for attempt in (default, run(cli, root, ALLOW, "--dns-server", resolver)):
            assert attempt.returncode == 3 and "HTTPS unavailable in this build" in attempt.stderr, attempt
        assert not requests.exists() and not resolver_requests.exists()
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
    for mode, expected in (("future", "invalid response observation time"), ("timeout", "deadline exceeded")):
        scenario.write_text(mode, encoding="ascii")
        result = run(cli, root, ALLOW)
        assert result.returncode == 3 and expected in result.stderr and not result.stdout, result
        assert requests.read_text().splitlines() == ["6"] and not resolver_requests.exists()
        requests.unlink()
    if os.name != "nt":
        scenario.write_text("slow", encoding="ascii")
        with subprocess.Popen([cli, "market-reward", ALLOW], cwd=root, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True) as child:
            try:
                deadline = time.monotonic() + 4
                while child.poll() is None and time.monotonic() < deadline:
                    if requests.exists() and requests.read_text().splitlines() == ["6"]:
                        break
                    time.sleep(0.02)
                else:
                    raise AssertionError("reward helper did not start")
                child.send_signal(signal.SIGINT)
                out, error = child.communicate(timeout=4)
                assert child.returncode == 130 and not out and "sample cancelled" in error
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait(timeout=3)
        requests.unlink()
    assert config.read_text() == "private-sentinel"
    assert {x.name for x in root.iterdir()} == {config.name, installed.name}
    print("Public reward checks consent, one fixed request, DNS forwarding, clock bound and cancellation")
    return 0


if __name__ == "__main__":
    cli = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="canary-market-reward-") as temporary:
        root = Path(temporary)
        if sys.argv[2] == "--offline":
            offline(cli, root)
        else:
            sys.exit(public(cli, Path(sys.argv[2]).resolve(), sys.argv[3] == "1", root))
