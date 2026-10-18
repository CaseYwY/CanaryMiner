#!/usr/bin/env python3
"""End-to-end Pearl pool test: the real miner binary against a mock gateway.

WHY. Pearl had nine device gates, byte-identical to the CUDA reference on two
vendors, and until this test nothing had ever run the binary against a server.
That is the shape of coverage that hid a share-counting bug in two other
algorithms on this fleet for weeks: vectors passed, device tests passed, and
the first mock-pool run found it.

`--bench` cannot substitute. It opens no socket, so authorize, notify parsing,
submit encoding, the accept/reject verdict and the counters are all unexercised
by it.

Assertions are made against --pearl-transcript, not stdout. The transcript is
JSONL and its submit record already carries header_b3, the m/n/k/rank read back
OUT OF THE SERIALISED PROOF rather than from the miner's own variables, and
digest_be against resolved_target_be - so this checks the arithmetic the pool is about to
redo, not merely that a submit happened. Counters are still compared against
what the pool actually credited, because a transcript cannot see a counter
wired to nothing.

Usage: python3 tests/test_pearl_pool_e2e.py [path-to-canaryminer]
"""
import hashlib
import json
import math
import os
import re
import socket
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pearl_mock import (  # noqa: E402
    EASY_SHARE_NBITS,
    EASY_TARGET,
    HEADER_NBITS,
    OFFICIAL_NEGATIVE_CONTROLS_PER_PROOF,
    PearlMock,
    header_hex,
)
from pearl_verifier_identity import verifier_extension_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TIMEOUT = 180
VERIFY_OFFICIAL = os.environ.get("CANARY_PEARL_OFFICIAL_VERIFY") == "1"

# These are verifier-security fixes, not merely convenient baselines. A
# package version is insufficient: both vulnerable and corrected verifier
# states have reported pearl-mining 0.3.1.
REQUIRED_VERIFIER_ANCESTORS = {
    "share-target override verification":
        "74379c255b1aee5f96653de3aeb24dd1512f7c5c",
    "canonical proof-object decoding":
        "735adc5d7cdec38756ca59883cd966eabcc4d23f",
    "rank-penalty consensus rule":
        "51a6e11bef44302e573c14db67cee42380c41d51",
    "V3 dimension-salted noise seeds":
        "fc5ca65a1df0fad0140e74c3b52e71c4a0f99e90",
    "Merkle tree leaf counts bound to declared dimensions":
        "10a0ee1851dd32328bc35c89d1e285090e5d5be9",
    "checked proof-dimension narrowing (CWE-681)":
        "4a0c24bc18dcb67cebca93c1ad38fb04837cb1a0",
}

# Synthetic, and the right shape for the local wallet check in run.cpp. Never a
# real address in a fixture: this file is committed.
WALLET = "prl1qq" + "q" * 56

FAILURES = []


def fail(msg):
    FAILURES.append(msg)
    print("   FAIL: %s" % msg)


def ok(msg):
    print("   ok: %s" % msg)


def expected_proof_shape():
    """Return the proof dimensions the evidence run claims to exercise.

    CANARY_PEARL_SHAPE tells the miner what to run.  This separate expectation
    is deliberate: the test must decode the submitted bytes and independently
    prove that the miner honoured the request.
    """
    raw = os.environ.get("CANARY_PEARL_EXPECT_SHAPE", "")
    if not raw:
        return None
    match = re.fullmatch(r"([1-9][0-9]*)x([1-9][0-9]*)", raw)
    if not match:
        raise ValueError(
            "CANARY_PEARL_EXPECT_SHAPE must be positive MxN dimensions, got %r"
            % raw)
    return int(match.group(1)), int(match.group(2))


def mock_is_clean(mock):
    clean = True
    for error in mock.errors:
        fail("mock: " + error)
        clean = False
    for violation in mock.violations:
        fail("protocol/verifier: " + violation)
        clean = False
    return clean


def official_verifier_identity():
    """Fail closed unless the verifier is tied to a clean, exact source SHA."""
    source = os.environ.get("CANARY_PEARL_VERIFIER_SOURCE", "")
    expected = os.environ.get("CANARY_PEARL_VERIFIER_REV", "").lower()
    expected_module_hash = os.environ.get(
        "CANARY_PEARL_VERIFIER_SHA256", "").lower()
    if not source or not re.fullmatch(r"[0-9a-f]{40}", expected):
        fail("official mode requires CANARY_PEARL_VERIFIER_SOURCE and a full "
             "40-hex CANARY_PEARL_VERIFIER_REV")
        return False
    if not re.fullmatch(r"[0-9a-f]{64}", expected_module_hash):
        fail("official mode requires the built extension's full "
             "CANARY_PEARL_VERIFIER_SHA256")
        return False
    source = os.path.realpath(source)
    try:
        actual = subprocess.run(
            ["git", "-C", source, "rev-parse", "HEAD"], check=True,
            capture_output=True, text=True, timeout=30).stdout.strip().lower()
        dirty = subprocess.run(
            ["git", "-C", source, "status", "--porcelain"], check=True,
            capture_output=True, text=True, timeout=30).stdout.strip()
    except (OSError, subprocess.SubprocessError) as exc:
        fail("cannot identify Pearl verifier source: %s" % exc)
        return False
    if actual != expected:
        fail("Pearl verifier revision is %s, expected %s" % (actual, expected))
        return False
    if dirty:
        fail("Pearl verifier source is dirty; use a pristine pinned worktree")
        return False
    for description, required in REQUIRED_VERIFIER_ANCESTORS.items():
        ancestry = subprocess.run(
            ["git", "-C", source, "merge-base", "--is-ancestor",
             required, actual], capture_output=True, text=True, timeout=30)
        if ancestry.returncode != 0:
            fail("Pearl verifier revision lacks required security fix %s (%s)"
                 % (required, description))
            return False
    try:
        import pearl_mining
    except ImportError as exc:
        fail("pearl_mining is not importable from this Python: %s" % exc)
        return False
    try:
        module_path = verifier_extension_path(pearl_mining)
    except (OSError, ValueError) as exc:
        fail("cannot identify native Pearl verifier extension: %s" % exc)
        return False
    try:
        in_source = os.path.commonpath((source, module_path)) == source
    except ValueError:
        in_source = False
    if not in_source:
        fail("pearl_mining loaded outside the pinned source tree: %s" % module_path)
        return False
    try:
        with open(module_path, "rb") as module_file:
            actual_module_hash = hashlib.sha256(module_file.read()).hexdigest()
    except OSError as exc:
        fail("cannot hash native Pearl verifier extension: %s" % exc)
        return False
    if actual_module_hash != expected_module_hash:
        fail("pearl_mining module sha256 is %s, expected %s" %
             (actual_module_hash, expected_module_hash))
        return False
    print("official verifier source: %s" % source)
    print("official verifier revision: %s" % actual)
    print("official verifier module: %s" % module_path)
    print("official verifier module sha256: %s" % actual_module_hash)
    print("official verifier package version: %s" %
          getattr(pearl_mining, "__version__", "unreported"))
    return True


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


# --------------------------------------------------------------- phase 0
def prove_mock_discriminates():
    """A mock that cannot fail proves nothing. Feed it each bad message and
    confirm it records exactly one violation, and none for a good one."""
    print("== phase 0: the mock discriminates (no GPU needed) ==")
    synthetic_header = bytes.fromhex(header_hex(1))
    if len(synthetic_header) != 76:
        fail("synthetic Pearl header is %d bytes, expected 76" % len(synthetic_header))
    elif int.from_bytes(synthetic_header[-4:], "little") != HEADER_NBITS:
        fail("synthetic header nBits does not match HEADER_NBITS")
    elif HEADER_NBITS == EASY_SHARE_NBITS:
        fail("block and share nBits must differ or the override is untested")
    else:
        exponent = EASY_SHARE_NBITS >> 24
        mantissa = EASY_SHARE_NBITS & 0x007FFFFF
        target = mantissa << (8 * (exponent - 3))
        if target.to_bytes(32, "big").hex() != EASY_TARGET:
            fail("share nBits does not encode EASY_TARGET")
        else:
            ok("header target differs; share nBits exactly encodes notify.target")
    cases = [
        ("array params",
         {"id": 1, "method": "mining.authorize", "params": [WALLET, "x"]}, 1),
        ("a `login` field instead of `wallet`",
         {"id": 1, "method": "mining.authorize",
          "params": {"login": WALLET}}, 1),
        ("an Ergo address",
         {"id": 1, "method": "mining.authorize",
          "params": {"wallet": "9f4QF8AD1nQ3nJahQVkMj8hFSVVzVom77b52JU7EW71Zexg6N8v"}}, 1),
        ("a proof that does not decode",
         {"id": 2, "method": "mining.submit",
          "params": {"job_id": "j", "plain_proof": "not base64!!"}}, 1),
        ("a well-formed authorize", 
         {"id": 1, "method": "mining.authorize",
          "params": {"wallet": WALLET, "worker": "rig"}}, 0),
    ]
    for name, msg, want in cases:
        port = free_port()
        m = PearlMock(port, total_shares=99)
        t = threading.Thread(target=m.serve, daemon=True)
        t.start()
        time.sleep(0.2)
        c = socket.create_connection(("127.0.0.1", port), timeout=10)
        c.sendall((json.dumps(msg) + "\n").encode())
        time.sleep(0.4)
        m.done.set()
        c.close()
        t.join(timeout=5)
        got = len(m.violations)
        if got != want:
            fail("mock recorded %d violation(s) for %s, expected %d: %s"
                 % (got, name, want, m.violations))
        else:
            ok("%s -> %d violation(s)" % (name, got))


# --------------------------------------------------------------- running
def pick_binary(argv):
    """Ask the binary which algorithms it has, rather than guessing from its
    name. This keeps working unchanged the day the Vulkan registry line is
    uncommented and canaryminer-vk grows pearl-pow too."""
    if len(argv) > 1:
        cands = [argv[1]]
    else:
        cands = [os.path.join(ROOT, n) for n in ("canaryminer", "canaryminer-vk")]
    for c in cands:
        if not os.path.exists(c):
            continue
        try:
            out = subprocess.run([c, "--list-algos"], capture_output=True,
                                 text=True, timeout=60).stdout
        except (OSError, subprocess.SubprocessError):
            continue
        if "pearl-pow" in out.split():
            return c
    return None


def observe_first_wire_reply(timing, event, mock, outcome):
    """Timestamp only the requested first wire operation, before teardown."""
    if (not mock.submits or type(event) is not dict or
            event.get("event") != "mining.reply" or event.get("outcome") != outcome):
        return False
    expected = mock.submits[0].get("id")
    ident = event.get("request_id")
    if (type(expected) is not int or not 0 < expected <= 2147483647 or
            type(ident) is not int or expected != ident):
        return False
    if "outcome_observed_at" not in timing:
        timing.update(outcome_observed_at=time.monotonic(), request_id=ident,
                      outcome=outcome, first_submit_received_at=mock.first_submit_received_at)
    return True


def unknown_reply_elapsed(timing, mock):
    """Observed client deadline upper bound; never include teardown duration."""
    if (not mock.submits or type(mock.submits[0].get("id")) is not int or
            not 0 < mock.submits[0]["id"] <= 2147483647 or
            timing.get("request_id") != mock.submits[0]["id"] or
            type(timing.get("request_id")) is not int or timing.get("outcome") != "unknown"):
        raise ValueError("missing correlated unknown observation")
    received, observed = timing.get("first_submit_received_at"), timing.get("outcome_observed_at")
    fences = [received, observed, timing.get("settle_started_at"),
              timing.get("shutdown_requested_at"), timing.get("process_reaped_at"),
              timing.get("stdout_joined_at"), timing.get("server_joined_at")]
    if (any(type(x) not in (int, float) or not math.isfinite(x)
            for x in fences + [mock.first_submit_received_at]) or
            received != mock.first_submit_received_at or received < 0 or
            any(a > b for a, b in zip(fences, fences[1:]))):
        raise ValueError("invalid operation/shutdown timing fences")
    return observed - received


def run_case(binary, mock, transcript, extra_args=(),
             stop_reply_outcome=None, settle_seconds=2.5, timing=None):
    timing = {} if timing is None else timing
    timing.clear()
    # DELETE THE TRANSCRIPT FIRST, AND REQUIRE A NEW ONE.
    #
    # Without this a run reads whatever the last one left at the same path.
    # That is not hypothetical: a Vulkan binary that silently ignored
    # --pearl-transcript produced no file, the reader found the previous CUDA
    # run's, and every transcript assertion passed while testing a backend that
    # had not written a byte. A test passing against stale state is the same
    # failure as a mutation test passing against a stale binary.
    if os.path.exists(transcript):
        os.remove(transcript)
    t = threading.Thread(target=mock.serve, daemon=True)
    t.start()
    time.sleep(0.3)
    proc = subprocess.Popen(
        [binary, "--algo", "pearl-pow", "--pool", "127.0.0.1:%d" % mock.port,
         "--pearl-network", "mainnet",
         "--pearl-target-semantics", "base-before-penalty",
         "--wallet", WALLET, "--worker", "mocktest",
         "--pearl-transcript", transcript, "--plain", "--interval", "1",
         *extra_args],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    lines = []

    def drain():
        for ln in proc.stdout:
            lines.append(ln.rstrip())

    d = threading.Thread(target=drain, daemon=True)
    d.start()

    deadline = time.monotonic() + TIMEOUT
    while time.monotonic() < deadline:
        if stop_reply_outcome and os.path.exists(transcript):
            try:
                with open(transcript) as stream:
                    for line in stream:
                        if not line.strip():
                            continue
                        event = json.loads(line)
                        if observe_first_wire_reply(timing, event, mock, stop_reply_outcome):
                            break
                    else:
                        event = None
                    if event is not None:
                        break
            except (OSError, ValueError):
                # The writer may have flushed between our existence check and
                # read. The normal transcript parser reports persistent errors.
                pass
        if mock.done.wait(timeout=0.5):
            break
        if proc.poll() is not None:
            break
    # One more telemetry interval, so a counter printed after the last verdict
    # is in the output we assert on.
    timing["settle_started_at"] = time.monotonic()
    time.sleep(settle_seconds)
    timing["shutdown_requested_at"] = time.monotonic()
    mock.done.set()
    proc.terminate()
    try:
        proc.wait(timeout=15)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)
    timing["process_reaped_at"] = time.monotonic()
    d.join(timeout=5)
    timing["stdout_joined_at"] = time.monotonic()
    t.join(timeout=5)
    timing["server_joined_at"] = time.monotonic()
    if stop_reply_outcome:
        with open(transcript + ".harness-timing.json", "w") as stream:
            json.dump({"schema": "pearl-e2e-operation-observation-v1", **timing}, stream, indent=2)
    # Retain stdout and synthetic proof messages, not authorization params,
    # so a failure can be inspected without another GPU maintenance window.
    with open(transcript + ".stdout.log", "w") as stream:
        stream.write("\n".join(lines) + "\n")
    if t.is_alive():
        fail("mock server did not stop; wire evidence is not final")
    else:
        with open(transcript + ".wire-submits.json", "w") as stream:
            json.dump(mock.submits, stream, indent=2)

    events = []
    if not os.path.exists(transcript):
        fail("the miner wrote no transcript at %s - does this binary support "
             "--pearl-transcript?" % transcript)
    if os.path.exists(transcript):
        with open(transcript) as stream:
            for ln in stream:
                ln = ln.strip()
                if ln:
                    try:
                        events.append(json.loads(ln))
                    except ValueError:
                        fail("transcript line is not JSON: %r" % ln[:120])
    return events, "\n".join(lines)


def reported_accepted(text):
    """Last valid JSON counter pair, or legacy text when no JSON pair exists.

    Look up fields rather than assuming telemetry ordering/adjacency. Keep
    `rejected` semantics unchanged; `pool_rejected` is a distinct counter.
    """
    acc = rej = None
    legacy_lines = []

    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate JSON field")
            result[key] = value
        return result

    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            record = json.loads(line, object_pairs_hook=unique_object)
        except ValueError:
            # Do not reinterpret truncated/malformed JSON as legacy counters.
            if not line.startswith(("{", "[", '"')):
                legacy_lines.append(line)
            continue
        if not isinstance(record, dict):
            continue
        accepted, rejected = record.get("accepted"), record.get("rejected")
        # bool is an int subclass in Python, but is not a telemetry count.
        if (type(accepted) is int and accepted >= 0 and
                type(rejected) is int and rejected >= 0):
            acc, rej = accepted, rejected
    if acc is None:
        for line in legacy_lines:
            for m in re.finditer(
                    r'\baccepted[ =:]+([0-9]+)(?![\w.+-]).{0,20}?'
                    r'\brejected[ =:]+([0-9]+)(?![\w.+-])', line, re.I):
                acc, rej = int(m.group(1)), int(m.group(2))
    return acc, rej


def of(events, ev):
    return [e for e in events if e.get("event") == ev]


def canonical_submit_matches_notify(submit, notifies, wire_id, certificate):
    """Diagnostic correlation only; not cryptographic proof acceptance."""
    if not isinstance(wire_id, str) or not wire_id:
        return False
    wanted_id = wire_id.encode("utf-8").hex()
    if (submit.get("schema") != "canary-pearl-canonical-diagnostic-v1" or
            submit.get("event") != "mining.submit" or
            submit.get("transport_job_id_hex") != wanted_id or
            submit.get("network") != "mainnet" or
            submit.get("target_semantics") != "base-before-penalty" or
            submit.get("cert_version") != "v%d" % certificate or
            type(submit.get("height")) is not int or submit["height"] <= 0):
        return False
    for field, length in (("header_b3", 16), ("raw_target_be", 64),
                          ("resolved_target_be", 64)):
        value = submit.get(field)
        if not isinstance(value, str) or not re.fullmatch("[0-9a-f]{%d}" % length, value):
            return False
    fields = ("schema", "network", "target_semantics", "cert_version", "height",
              "transport_job_id_hex", "header_b3", "raw_target_be", "resolved_target_be")
    return any(n.get("event") == "mining.notify" and
               all(n.get(field) == submit[field] for field in fields)
               for n in notifies)


def reply_conservation(events, wire_submits, phase):
    # The mock deliberately closes after its quota. A subsequent send can
    # succeed locally without being read by the mock, then terminate UNKNOWN.
    # Conserve the client's attempts, and independently bind the received
    # prefix to wire request IDs; never promote an unseen attempt to accepted.
    submits = of(events, "mining.submit")
    replies = of(events, "mining.reply")
    submit_ids = [event.get("request_id") for event in submits]
    reply_ids = [event.get("request_id") for event in replies]
    wire_ids = [event.get("id") for event in wire_submits]
    ids_valid = all(type(value) is int and value > 0
                    for value in submit_ids + reply_ids + wire_ids)
    correlated = (ids_valid and len(set(submit_ids)) == len(submit_ids)
                  and reply_ids == submit_ids
                  and wire_ids == submit_ids[:len(wire_ids)])
    if not correlated:
        fail("%s request-ID conservation failed: attempts=%r replies=%r wire=%r"
             % (phase, submit_ids, reply_ids, wire_ids))
    pending_ids = []
    for event in events:
        if event.get("event") == "mining.submit":
            pending_ids.append(event.get("request_id"))
        elif event.get("event") == "mining.reply":
            if (not pending_ids or
                    pending_ids.pop(0) != event.get("request_id")):
                fail("%s reply preceded its matching attempt" % phase)
    outcomes = ("accepted", "accepted_superseded", "rejected", "stale",
                "unknown")
    buckets = {name: 0 for name in outcomes}
    for reply in replies:
        outcome = reply.get("outcome")
        if outcome not in buckets:
            fail("%s emitted an unknown reply outcome %r" % (phase, outcome))
            continue
        flags = {"accepted": outcome in ("accepted", "accepted_superseded"),
                 "accepted_superseded": outcome == "accepted_superseded",
                 "stale": outcome == "stale", "unknown": outcome == "unknown"}
        if any(reply.get(name) is not value for name, value in flags.items()):
            fail("%s reply flags contradict its outcome" % phase)
        buckets[outcome] += 1
    submit_count = len(submits)
    for reply in replies[len(wire_ids):]:
        if reply.get("outcome") != "unknown":
            fail("%s unobserved wire attempt has a non-UNKNOWN outcome" % phase)
    if len(replies) != submit_count or sum(buckets.values()) != submit_count:
        fail("%s reply conservation failed: submits=%d replies=%d buckets=%r"
             % (phase, submit_count, len(replies), buckets))
    elif correlated:
        ok("%s conserved %d client attempts (%d observed by mock) across outcomes"
           % (phase, submit_count, len(wire_ids)))
    return replies, buckets


# ---------------------------------------------------------------- phases
def phase_auth_correlation(binary, tmp):
    print("== phase 1: authorization waits for its exact request id ==")
    m = PearlMock(free_port(), total_shares=99,
                  unrelated_auth_success_then_reject=True)
    tr = os.path.join(tmp, "auth-id.jsonl")
    _, out = run_case(binary, m, tr)
    mock_is_clean(m)
    if m.submits:
        fail("miner submitted after the correlated id:1 authorization failed")
    elif "mock auth rejection" not in out:
        fail("unrelated result:true authorized the session; output was:\n%s"
             % out[-1500:])
    else:
        ok("ignored unrelated success and honored the id:1 rejection")


def phase_auth_deadline(binary, tmp):
    print("== phase 2: fragmented auth replies cannot extend post-connect deadline ==")
    m = PearlMock(free_port(), total_shares=99,
                  hold_auth_reply_seconds=27)
    tr = os.path.join(tmp, "auth-deadline.jsonl")
    started = time.monotonic()
    _, out = run_case(binary, m, tr)
    elapsed = time.monotonic() - started
    mock_is_clean(m)
    if m.submits:
        fail("miner submitted without a complete authorization reply")
    elif "within 25 seconds" not in out:
        fail("fragmented authorization did not surface its hard deadline:\n%s"
             % out[-1500:])
    elif elapsed >= 32:
        fail("authorization deadline phase took %.1fs" % elapsed)
    else:
        ok("authorization reply stopped under one post-connect deadline")


def phase_accept(binary, tmp, expected_shape=None, cert_version=3):
    verifier = " by Pearl's official verifier" if VERIFY_OFFICIAL else ""
    print("== phase 3 V%d: a share is submitted, verified%s and counted ==" %
          (cert_version, verifier))
    # Pearl may legally push work before acknowledging authorization. The
    # client must keep that work while still waiting for the matching id:1.
    m = PearlMock(free_port(), total_shares=1, verify_submits=VERIFY_OFFICIAL,
                  notify_before_auth_reply=True, cert_version=cert_version)
    tr = os.path.join(tmp, "accept-v%d.jsonl" % cert_version)
    ev, out = run_case(binary, m, tr)

    mock_is_clean(m)

    if m.authorize is None:
        fail("the miner never authorized")
        return
    ok("authorized with params %s" % sorted(m.authorize.get("params", {})))

    if not m.submits:
        fail("no share submitted in %ds against a 2^228 base / 2^247 "
             "penalized bound; miner said:\n%s"
             % (TIMEOUT, out[-2500:]))
        return
    ok("%d share(s) submitted" % len(m.submits))
    if VERIFY_OFFICIAL:
        if m.official_verified != len(m.submits):
            fail("official verifier approved %d of %d submit(s)" %
                 (m.official_verified, len(m.submits)))
        elif (m.official_negative_controls !=
              OFFICIAL_NEGATIVE_CONTROLS_PER_PROOF * len(m.submits)):
            fail("official verifier ran %d negative controls, expected %d" %
                 (m.official_negative_controls,
                  OFFICIAL_NEGATIVE_CONTROLS_PER_PROOF * len(m.submits)))
        else:
            ok("official verifier accepted the proof; %d negative controls failed closed"
               % OFFICIAL_NEGATIVE_CONTROLS_PER_PROOF)

    sp = m.submits[0]["params"]
    if sp["job_id"] != m.notified[-1] and sp["job_id"] not in m.notified:
        fail("submitted job_id %r was never notified" % sp["job_id"])
    else:
        ok("submit names a job the pool actually sent")

    subs = of(ev, "mining.submit")
    nots = of(ev, "mining.notify")
    if not subs:
        fail("the transcript recorded no mining.submit")
        return
    if not nots:
        fail("the transcript recorded no mining.notify")
    s = subs[0]

    # The arithmetic the pool is about to redo.
    if s.get("digest_within_bound") is not True:
        fail("digest_within_bound=%r: we submitted a proof above our own bound "
             "(digest_be=%s resolved_target_be=%s)"
             % (s.get("digest_within_bound"), s.get("digest_be"), s.get("resolved_target_be")))
    else:
        ok("digest is within the canonical policy-resolved bound")

    # Shape, read out of the serialised proof rather than our own variables.
    if not (s.get("k") and s.get("rank")):
        fail("proof shape did not decode: m=%r n=%r k=%r rank=%r"
             % (s.get("m"), s.get("n"), s.get("k"), s.get("rank")))
    else:
        ok("proof decodes to m=%s n=%s k=%s rank=%s"
           % (s.get("m"), s.get("n"), s.get("k"), s.get("rank")))
        # The shape identifies which backend really ran: the CUDA path tunes
        # across several and the Vulkan one is fixed. A run that looked like it
        # exercised Vulkan but reported CUDA's shape is what made this line
        # worth printing rather than only checking.
    if expected_shape is not None:
        # Decode the actual proof received by the mock, with the separately
        # built and hash-pinned official binding.  The transcript's m/n fields
        # are decoded by the miner under test, so trusting only those would let
        # a shared decoder/reporting bug satisfy its own assertion.
        try:
            import pearl_mining
            wire_proof = pearl_mining.PlainProof.from_base64(
                sp["plain_proof"])
            actual_shape = (int(wire_proof.m), int(wire_proof.n))
        except Exception as exc:                         # noqa: BLE001
            fail("official binding could not decode submitted proof shape: %s"
                 % exc)
        else:
            if actual_shape != expected_shape:
                fail("wire proof shape is %sx%s, expected pinned %sx%s"
                     % (actual_shape[0], actual_shape[1],
                        expected_shape[0], expected_shape[1]))
            else:
                ok("official binding decodes pinned production shape %sx%s"
                   % expected_shape)
            transcript_shape = (s.get("m"), s.get("n"))
            if transcript_shape != actual_shape:
                fail("miner transcript shape %sx%s disagrees with official "
                     "wire-proof decode %sx%s"
                     % (transcript_shape[0], transcript_shape[1],
                        actual_shape[0], actual_shape[1]))
            else:
                ok("miner transcript shape matches official wire-proof decode")

    # The proof was mined against the job it names.
    if not canonical_submit_matches_notify(s, nots, sp["job_id"], cert_version):
        fail("canonical submit identity/target is not bound to its exact notify")
    else:
        ok("canonical header, source target and resolved bound match the wire job")

    reps = of(ev, "mining.reply")
    if not reps or reps[0].get("accepted") is not True:
        fail("the pool accepted, the transcript says %r"
             % (reps[0] if reps else None))
    else:
        ok("the accept is recorded as an accept")
    reply_conservation(ev, m.submits, "accept")

    acc, rej = reported_accepted(out)
    if acc is None:
        fail("the miner never printed a share counter:\n%s" % out[-1500:])
    elif acc != m.credited or rej != 0:
        fail("the miner reports accepted=%s rejected=%s, pool credited %d"
             % (acc, rej, m.credited))
    else:
        ok("reported accepted=%d matches the %d the pool credited"
           % (acc, m.credited))

    # The transcript is committed evidence in a bug report. It must not carry
    # the payout address, and pearlTranscriptRedact must have run.
    blob = open(tr).read()
    if WALLET in blob:
        fail("the wallet appears verbatim in the transcript")
    else:
        ok("no wallet in the transcript")


def phase_reject(binary, tmp):
    print("== phase 4: a rejection is not counted as an accept ==")
    m = PearlMock(free_port(), total_shares=1, reject_from=0,
                  verify_submits=VERIFY_OFFICIAL)
    tr = os.path.join(tmp, "reject.jsonl")
    ev, out = run_case(binary, m, tr)
    mock_is_clean(m)
    if not m.submits:
        fail("no share submitted, so the rejection path was never reached")
        return
    if VERIFY_OFFICIAL and m.official_verified != len(m.submits):
        fail("scripted reject did not first pass the official verifier")
    reps = of(ev, "mining.reply")
    if not reps or reps[0].get("accepted") is not False:
        fail("a rejected share was recorded as %r" % (reps[0] if reps else None))
    else:
        ok("recorded as rejected, reason %r" % reps[0].get("reason"))
    reply_conservation(ev, m.submits, "reject")
    acc, rej = reported_accepted(out)
    if acc is None:
        fail("no counter printed")
    elif acc != 0 or rej != len(m.submits):
        fail("rejected share counters are accepted=%s rejected=%s for %d submit(s)"
             % (acc, rej, len(m.submits)))
    else:
        ok("accepted stayed 0 (pool credited %d), rejected=%s" % (m.credited, rej))


def phase_accepted_superseded(binary, tmp):
    print("== phase 5: same-session in-flight work gets the pool's verdict ==")
    # Each share is already in flight when the pool pushes its replacement.
    # This phase intentionally requests one nonce attempt per batch: the CLI
    # minimum 256 work units rounds up to one complete grid for every supported
    # CUDA shape. openWin emits at most one proof per attempt. Thus after the
    # in-flight verdict, the next search must fetch the replacement job.
    # Production multi-attempt batches may legitimately finish dispatching
    # several already-owned proofs; do not impose this phase's rule on them.
    m = PearlMock(free_port(), total_shares=3, renotify_before_reply=True,
                  verify_submits=VERIFY_OFFICIAL)
    tr = os.path.join(tmp, "stale.jsonl")
    ev, out = run_case(binary, m, tr, extra_args=("--batch", "256"))
    mock_is_clean(m)
    if len(m.submits) < 2:
        fail("only %d submit(s); the guard needs at least two jobs"
             % len(m.submits))
        return
    ok("%d shares across %d jobs" % (len(m.submits), len(m.notified)))
    order = {j: i for i, j in enumerate(m.notified)}
    for n, s in enumerate(m.submits):
        jid = s["params"]["job_id"]
        if jid not in order:
            fail("submitted an unknown job_id %r" % jid)
            continue
        if jid != m.newest_at_submit[n]:
            fail("share %d named %r instead of newest consumed job %r"
                 % (n, jid, m.newest_at_submit[n]))
        if n > 0 and order[jid] <= order[m.newest_at_submit[n - 1]]:
            fail("share %d did not advance to the replacement after the prior "
                 "in-flight verdict" % n)
    ok("in-flight old work was sent once; later submissions used replacements")
    reps, _ = reply_conservation(ev, m.submits, "accepted-superseded")
    # Only the wire-observed prefix received the scripted superseded verdict.
    # A later locally sent attempt can terminate UNKNOWN after the mock closes
    # at its quota; reply_conservation independently requires that suffix to
    # remain UNKNOWN and keeps every client attempt correlated exactly once.
    observed_reps = reps[:len(m.submits)]
    if (len(observed_reps) != len(m.submits) or
            any(reply.get("request_id") != wire.get("id") or
                reply.get("outcome") != "accepted_superseded"
                for reply, wire in zip(observed_reps, m.submits))):
        fail("superseded accepts lost their diagnostic: %r" % observed_reps)
    elif m.accepted_superseded != len(m.submits):
        fail("mock accepted_superseded=%d for %d submits"
             % (m.accepted_superseded, len(m.submits)))
    else:
        ok("%d result:true replies retained accepted-superseded timing"
           % m.accepted_superseded)
    acc, rej = reported_accepted(out)
    if acc is None:
        fail("no counter printed for accepted-superseded phase")
    elif acc != m.credited or rej != 0:
        fail("superseded accepts counted incorrectly: accepted=%s rejected=%s "
             "credited=%d" % (acc, rej, m.credited))
    else:
        ok("accepted aggregate includes each accepted-superseded share once")
    if "ACCEPTED a share after replacing its job" not in out:
        fail("accepted-superseded outcome was absent from normal output")
    else:
        ok("accepted-superseded outcome is visible without a transcript")


def phase_explicit_stale(binary, tmp):
    print("== phase 6: false plus exact stale text is stale ==")
    m = PearlMock(free_port(), total_shares=1,
                  stale_reply_text="Job not found",
                  verify_submits=VERIFY_OFFICIAL)
    tr = os.path.join(tmp, "explicit-stale.jsonl")
    ev, out = run_case(binary, m, tr)
    mock_is_clean(m)
    reps, _ = reply_conservation(ev, m.submits, "explicit-stale")
    if not reps or reps[0].get("outcome") != "stale":
        fail("Job not found result:false recorded as %r"
             % (reps[0] if reps else None))
    else:
        ok("Job not found result:false recorded stale")
    acc, rej = reported_accepted(out)
    if acc is None:
        fail("no counter printed for explicit-stale phase")
    elif acc != 0 or rej != 0:
        fail("explicit stale polluted counters: accepted=%d rejected=%d"
             % (acc, rej))
    else:
        ok("accepted/rejected stayed zero for explicit stale reply")
    if "marked a share STALE" not in out:
        fail("explicit stale outcome was absent from normal output")
    else:
        ok("explicit stale outcome is visible without a transcript")


def unknown_reply_counters(events, output):
    """Keep legacy rejected = host-invalid + local-failure + pool-rejected.

    A partial held reply can trigger one later pre-delivery local refusal. It
    must not become an extra wire attempt or a fabricated pool stale/rejection.
    """
    def unique(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate telemetry field")
            result[key] = value
        return result

    def nonfinite(_):
        raise ValueError("nonfinite telemetry")

    latest, local_failures = None, 0
    for line in output.splitlines():
        if not line.lstrip().startswith("{"):
            continue
        record = json.loads(line, object_pairs_hook=unique, parse_constant=nonfinite)
        if type(record) is not dict:
            continue
        message = record.get("msg")
        if (record.get("event") == "error" and type(message) is str and
                message.startswith("submission failed before acknowledgement")):
            if not message.endswith("partial pre-submit message; proof not sent"):
                raise ValueError("unexpected local failure in held-reply phase")
            local_failures += 1
        if "uptime_s" in record:
            latest = record
    if latest is None or local_failures not in (0, 1):
        raise ValueError("missing telemetry or unbounded local NotSent failures")
    fields = ("submitted", "acknowledged", "accepted", "pool_rejected", "stale",
              "unknown", "pending", "accepted_superseded", "host_invalid", "submit_failed", "rejected")
    if any(type(latest.get(key)) is not int or not 0 <= latest[key] <= (1 << 64) - 1
           for key in fields):
        raise ValueError("missing or invalid typed outcome counter")
    submits, replies = of(events, "mining.submit"), of(events, "mining.reply")
    if (len(submits) != 1 or len(replies) != 1 or
            any(reply.get("outcome") != "unknown" for reply in replies) or
            latest["submitted"] != len(submits) or latest["unknown"] != len(replies) or
            any(latest[key] != 0 for key in ("acknowledged", "accepted", "pool_rejected",
                                           "stale", "pending", "accepted_superseded", "host_invalid")) or
            latest["submit_failed"] != local_failures or
            latest["rejected"] != latest["host_invalid"] + latest["submit_failed"] + latest["pool_rejected"]):
        raise ValueError("remote/local/legacy aggregate outcome conservation failed")
    return {key: latest[key] for key in fields}


def phase_unknown_reply(binary, tmp):
    print("== phase 7: partial open reply times out unknown, not rejected ==")
    m = PearlMock(free_port(), total_shares=1,
                  hold_submit_reply_seconds=60,
                  verify_submits=VERIFY_OFFICIAL)
    tr = os.path.join(tmp, "unknown-reply.jsonl")
    timing = {}
    ev, out = run_case(binary, m, tr, stop_reply_outcome="unknown",
                       settle_seconds=0.5, timing=timing)
    # Device initialization, tuning and proof construction are not a pending
    # submit. Measure from server receipt BEFORE official verification, so
    # verifier latency still counts. Timestamp the correlated transcript
    # observation BEFORE settling/termination/joins, recorded separately.
    try:
        elapsed = unknown_reply_elapsed(timing, m)
    except ValueError as error:
        elapsed = None
        fail("invalid pending-reply timing evidence: %s" % error)
    mock_is_clean(m)
    reps, _ = reply_conservation(ev, m.submits, "unknown-reply")
    # The held reply belongs to the one request the mock actually read.
    # The repaired pre-submit path must refuse a later partial-frame attempt
    # locally, without a second diagnostic/wire submission or UNKNOWN reply.
    # The strict counter contract below additionally enforces that exact cap.
    observed_reps = reps[:len(m.submits)]
    if (len(m.submits) != 1 or len(observed_reps) != 1 or
            observed_reps[0].get("outcome") != "unknown"):
        fail("held wire request was not recorded exactly once as unknown: %r"
             % observed_reps)
    else:
        ok("missing correlated reply recorded exactly once as unknown")
    if elapsed is None:
        fail("partial reply deadline was not exercised: no submit received")
    elif elapsed < 0 or elapsed >= 20:
        fail("partial open submit reply exceeded its 15-second deadline (%.1fs)"
             % elapsed)
    else:
        ok("partial open submit reply stopped under the operation deadline")
    try:
        counters = unknown_reply_counters(ev, out)
    except (ValueError, TypeError) as error:
        fail("unknown reply counter conservation failed: %s" % error)
    else:
        ok("unknown reply conserved remote outcomes and %d local NotSent failure(s); "
           "legacy rejected=%d" % (counters["submit_failed"], counters["rejected"]))
    if "share outcome UNKNOWN" not in out:
        fail("unknown outcome was absent from normal output")
    else:
        ok("unknown outcome is visible without a transcript")


def phase_push_burst(binary, tmp):
    print("== phase 8: a burst of pushes before the reply ==")
    # submit() reads at most 12 non-push replies looking for its own id. The
    # mock repeats the exact current job 16 times: this exercises framing and
    # the reply budget without superseding a share that the mock credits.
    m = PearlMock(free_port(), total_shares=1, push_burst=16,
                  verify_submits=VERIFY_OFFICIAL)
    tr = os.path.join(tmp, "burst.jsonl")
    ev, out = run_case(binary, m, tr)
    mock_is_clean(m)
    if not m.submits:
        fail("no share submitted")
        return
    reps, _ = reply_conservation(ev, m.submits, "push-burst")
    verdict = reps[0] if reps else None
    print("   pool credited %d, transcript reply %r" % (m.credited, verdict))
    if verdict is None:
        fail("the pool answered but the miner recorded no reply at all")
    elif verdict.get("accepted") is not True:
        fail("the pool accepted this share; the miner recorded accepted=%r "
             "reason=%r. 16 pushes arrived before the reply and the read "
             "window in submit() is 12 lines."
             % (verdict.get("accepted"), verdict.get("reason")))
    else:
        ok("the reply was found behind 16 pushes")
    acc, _ = reported_accepted(out)
    if acc is not None and acc != m.credited:
        fail("the pool credited %d, the miner reports accepted=%s"
             % (m.credited, acc))


def main():
    tmp = os.path.join(ROOT, "build", "pearl-e2e")
    os.makedirs(tmp, exist_ok=True)

    prove_mock_discriminates()

    try:
        expected_shape = expected_proof_shape()
    except ValueError as exc:
        fail(str(exc))
        return 1
    if expected_shape is not None:
        if not VERIFY_OFFICIAL:
            fail("CANARY_PEARL_EXPECT_SHAPE requires "
                 "CANARY_PEARL_OFFICIAL_VERIFY=1")
            return 1
        print("expected proof shape: %sx%s (decoded from submitted proof)"
              % expected_shape)

    if VERIFY_OFFICIAL and not official_verifier_identity():
        return 1

    binary = pick_binary(sys.argv)
    if binary is None:
        message = "no built binary reports pearl-pow in --list-algos"
        if VERIFY_OFFICIAL:
            fail(message + "; official-verifier mode cannot skip")
            return 1
        print("\nSKIP: " + message)
        return 1 if FAILURES else 0
    print("\nbinary: %s" % binary)
    print("proof verifier: %s" %
          ("Pearl official Rust binding (required)" if VERIFY_OFFICIAL
           else "mock structural checks only"))
    # SAY WHICH BACKEND RAN. A phase-1 line reading n=65536 when the Vulkan
    # shape is 16384 was the only clue that a run had not exercised what it
    # looked like it had, and it took a second run to settle. The banner is
    # cheap and removes the ambiguity.
    algos = subprocess.run([binary, "--list-algos"], capture_output=True,
                           text=True, timeout=60).stdout.split()
    print("backend:  %s  (algos: %s)"
          % (os.path.basename(binary), " ".join(algos)))

    phase_auth_correlation(binary, tmp)
    phase_auth_deadline(binary, tmp)
    for cert_version in (2, 3):
        phase_accept(binary, tmp, expected_shape, cert_version=cert_version)
    phase_reject(binary, tmp)
    phase_accepted_superseded(binary, tmp)
    phase_explicit_stale(binary, tmp)
    phase_unknown_reply(binary, tmp)
    phase_push_burst(binary, tmp)

    print()
    if FAILURES:
        print("FAIL: %d problem(s)" % len(FAILURES))
        return 1
    print("PASS: canaryminer speaks the Pearl pool protocol correctly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
