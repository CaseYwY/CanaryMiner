#!/usr/bin/env python3
"""A mock Pearl pool: enough of the gateway's stratum for the real miner.

Transcribed from the contract documented in src/core/pearl_pool.h, not from a
capture, so it stays readable and so a change to the contract shows up here as
a test failure rather than as a mystery on a live pool.

Pearl's stratum is NOT the array-params stratum the other algorithms use:

    -> {"id":1,"method":"mining.authorize","params":{"wallet":"prl1...",
                                                     "worker":"rig"}}
    <- {"id":1,"result":true,"error":null}
    <- {"id":null,"method":"mining.notify",
        "params":{"job_id":"...","header":"<152 hex>","target":"<64 hex BE>",
                  "height":N,"cert_version":3}}
    -> {"id":2,"method":"mining.submit",
        "params":{"job_id":"...","plain_proof":"<base64 bincode>"}}
    <- {"id":2,"result":true,"error":null}

The target is scaled by the miner before it is used as a bound - see
MiningConfig::penalizedTarget - so a target here must leave room for a
multiply by tileSize * (k/rank) * 128 without overflowing 256 bits.
"""
import base64
from dataclasses import dataclass
from enum import Enum
import json
import socket
import threading
import time

# 2^228. Bitcoin compact form 0x1d100000 represents this target exactly.
# Scaled by 2^19 that is a 2^247 bound, so about one candidate in 512 wins and
# a share arrives in well under a second on any real card.
EASY_SHARE_NBITS = 0x1D100000
EASY_TARGET = "00000010" + "00" * 28

# A deliberately different, much harder block target. A real pool sends jobs
# whose header nBits remains the network target while notify.target expresses
# the worker's easier share target. The official verifier must therefore use
# EASY_SHARE_NBITS as nbits_override, not silently verify at header difficulty.
HEADER_NBITS = 0x1B010000
IMPOSSIBLE_SHARE_NBITS = 0x01010000  # target 1, for a negative control

# Owned here, beside the controls themselves.  The E2E runner imports this
# rather than duplicating a literal that can silently fall behind the mock.
OFFICIAL_NEGATIVE_CONTROLS_PER_PROOF = 14

# Mainnet intervals pinned alongside pearl_job_context.h. V2 fixtures are
# deliberately after rank-penalty activation, so both cert paths exercise the
# same share-target rule; this is not a general historical-chain simulator.
MAINNET_RANK_PENALTY_HEIGHT = 96251
MAINNET_V3_HEIGHT = 99000
UINT64_MAX = (1 << 64) - 1
DEFAULT_HEIGHT_BASE = {2: 98000, 3: 100000}


def validate_fixture_height(cert_version, height):
    if type(cert_version) is not int or cert_version not in (2, 3):
        raise ValueError("fixture cert_version must be exactly 2 or 3")
    if type(height) is not int or not 0 < height <= UINT64_MAX:
        raise ValueError("fixture height must be a positive uint64")
    if cert_version == 2:
        allowed = MAINNET_RANK_PENALTY_HEIGHT <= height < MAINNET_V3_HEIGHT
    else:
        allowed = MAINNET_V3_HEIGHT <= height
    if not allowed:
        raise ValueError("fixture certificate is invalid at this mainnet height")


class VerificationKind(Enum):
    ACCEPTED = "accepted"
    REJECTED = "verifier-rejected"
    BINDING_REJECTED = "binding-rejected"
    ERROR = "adapter-error"


@dataclass(frozen=True)
class VerificationResult:
    kind: VerificationKind
    reason: str

# A synthetic 76-byte header. Never a captured one: a real header would tie
# this fixture to a chain height and to somebody's payout address.
def header_hex(salt=0):
    header = bytearray((i * 7 + 3 + salt) & 0xFF for i in range(76))
    header[72:76] = HEADER_NBITS.to_bytes(4, "little")
    return header.hex()


class PearlMock:
    """One connection, scripted. Every field a test asserts on is recorded."""

    def __init__(self, port, total_shares=1, reject_from=None,
                 renotify_before_reply=False, push_burst=0,
                 bad_notify_first=False, verify_submits=False,
                 notify_before_auth_reply=False,
                 unrelated_auth_success_then_reject=False,
                 stale_reply_text=None, hold_submit_reply_seconds=0,
                 hold_auth_reply_seconds=0, cert_version=3, height_base=None):
        if type(cert_version) is not int or cert_version not in (2, 3):
            raise ValueError("fixture cert_version must be exactly 2 or 3")
        if height_base is None:
            height_base = DEFAULT_HEIGHT_BASE[cert_version]
        if type(height_base) is not int:
            raise ValueError("fixture height_base must be an integer")
        validate_fixture_height(cert_version, height_base + 1)
        self.cert_version = cert_version
        self.height_base = height_base
        self.port = port
        self.total_shares = total_shares
        # 0-based index of the first submit to answer with an error.
        self.reject_from = reject_from
        # Push a fresh job BEFORE replying to each submit. The in-flight share
        # still belongs to this live TCP session and may be accepted during the
        # pool's retarget grace; the following submit must use the replacement.
        self.renotify_before_reply = renotify_before_reply
        # Number of duplicate current-job pushes to send BEFORE the reply to a
        # submit. They exercise the reply search window without superseding
        # the submitted job, so an accepted reply remains pool-creditable.
        self.push_burst = push_burst
        # Send one malformed notify first, which the miner must ignore rather
        # than mine against.
        self.bad_notify_first = bad_notify_first
        self.notify_before_auth_reply = notify_before_auth_reply
        self.unrelated_auth_success_then_reject = \
            unrelated_auth_success_then_reject
        self.stale_reply_text = stale_reply_text
        self.hold_submit_reply_seconds = hold_submit_reply_seconds
        self.hold_auth_reply_seconds = hold_auth_reply_seconds
        self.verify_submits = verify_submits
        self._pearl_mining = None
        if verify_submits:
            try:
                import pearl_mining
            except ImportError as exc:
                raise RuntimeError(
                    "verify_submits requires Pearl's pearl_mining extension"
                ) from exc
            self._pearl_mining = pearl_mining
        self.official_verified = 0
        self.official_negative_controls = 0

        self.authorize = None
        self.submits = []          # each: the decoded mining.submit message
        self.first_submit_received_at = None  # monotonic, before verification
        # Contract violations the pool would answer with an error code. Kept
        # separate from `errors` so the mock's own discrimination is testable:
        # feeding it a bad message must record exactly one of these.
        self.violations = []
        self.credited = 0          # shares the POOL counted, to compare against
                                   # the counter the miner reports
        self.stale = 0             # shares the pool filed as stale, not credited
        self.accepted_superseded = 0
        self.notified = []         # each: job_id, newest last
        self.jobs = {}             # job_id -> exact notify params
        self.newest_at_submit = []  # the newest job when each submit arrived
        self.errors = []
        self.done = threading.Event()
        self._n = 0

    # -- wire helpers ----------------------------------------------------
    def _send(self, conn, obj):
        conn.sendall((json.dumps(obj) + "\n").encode())

    def _notify(self, conn):
        next_n = self._n + 1
        height = self.height_base + next_n
        # Refuse a V2->V3 boundary crossing/uint64 exhaustion before changing
        # mock ownership or emitting a job with a contradictory certificate.
        validate_fixture_height(self.cert_version, height)
        self._n = next_n
        jid = "mock-%08x_2097152" % self._n
        self.notified.append(jid)
        params = {"job_id": jid, "header": header_hex(self._n),
                  "target": EASY_TARGET, "height": height,
                  "cert_version": self.cert_version}
        self.jobs[jid] = params
        self._send(conn, {"id": None, "method": "mining.notify", "params": params})
        return jid

    def _verify_with(self, job, proof_b64, cert_version=None,
                     header_bytes=None, share_nbits=EASY_SHARE_NBITS,
                     allow_decode_rejection=False):
        try:
            pm = self._pearl_mining
            raw_header = (bytes.fromhex(job["header"])
                          if header_bytes is None else header_bytes)
            header = pm.IncompleteBlockHeader.from_bytes(raw_header)
            try:
                proof = pm.PlainProof.from_base64(proof_b64)
            except (ValueError, OverflowError) as exc:
                if allow_decode_rejection:
                    return VerificationResult(
                        VerificationKind.BINDING_REJECTED,
                        "proof decoder rejected value: %s" % exc)
                raise
            accepted, reason = pm.verify_plain_proof_for_cert_version(
                job["cert_version"] if cert_version is None else cert_version,
                header, proof, nbits_override=share_nbits)
            if type(accepted) is not bool or not isinstance(reason, str):
                raise TypeError("verifier must return (bool, str)")
            if not accepted and not reason:
                raise ValueError("verifier rejection has no reason")
            return VerificationResult(
                VerificationKind.ACCEPTED if accepted else VerificationKind.REJECTED,
                reason)
        except Exception as exc:                         # noqa: BLE001
            return VerificationResult(VerificationKind.ERROR,
                                      "official verifier adapter error: %s" % exc)

    def _verify_proof(self, params):
        """Verify the exact submit and prove the verifier discriminates."""
        try:
            return self._verify_proof_checked(params)
        except Exception as exc:                         # noqa: BLE001
            return False, "official control construction error: %s" % exc

    def _verify_proof_checked(self, params):
        job = self.jobs.get(params.get("job_id"))
        if job is None:
            return False, "proof names a job the mock never notified"
        positive = self._verify_with(job, params["plain_proof"])
        if positive.kind is not VerificationKind.ACCEPTED:
            return False, positive.reason

        # Negative controls against the SAME CUDA-produced proof. A mock that
        # accepts any of these cannot establish consensus correctness.
        changed_header = bytearray.fromhex(job["header"])
        changed_header[4] ^= 1
        tests = [
            ("changed header", self._verify_with(
                job, params["plain_proof"], header_bytes=bytes(changed_header))),
            ("header block target without share override", self._verify_with(
                job, params["plain_proof"], share_nbits=None)),
            ("harder share target", self._verify_with(
                job, params["plain_proof"], share_nbits=IMPOSSIBLE_SHARE_NBITS)),
            ("opposite certificate derivation", self._verify_with(
                job, params["plain_proof"],
                cert_version=3 if job["cert_version"] == 2 else 2)),
        ]
        changed_proof = bytearray(base64.b64decode(params["plain_proof"], validate=True))
        changed_proof[len(changed_proof) // 2] ^= 1
        tests.append(("changed proof byte", self._verify_with(
            job, base64.b64encode(changed_proof).decode(),
            allow_decode_rejection=True)))
        trailing_proof = base64.b64decode(
            params["plain_proof"], validate=True) + b"\x00"
        tests.append(("trailing non-canonical proof byte", self._verify_with(
            job, base64.b64encode(trailing_proof).decode(),
            allow_decode_rejection=True)))

        # Defensive regressions for upstream verifier fix #299. Rebuild through
        # the public binding instead of hand-editing bincode offsets: values
        # wider than the consensus wire types must fail closed. A future secure
        # binding may reject a value before serialization, which is also a
        # successful negative result; unrelated API/type failures are not.
        pm = self._pearl_mining
        proof = pm.PlainProof.from_base64(params["plain_proof"])
        in_domain_variant_args = [
            # These four stay inside their consensus wire widths and ordinary
            # mining domains.  They are intentionally not overflow tests: the
            # unchanged Merkle openings must not remain valid after a material
            # proof configuration change.  Rank 64 is a supported noise rank
            # but is rejected by the current rank-penalty consensus rule.
            ("in-domain m change",
             (proof.m + proof.noise_rank, proof.n, proof.k,
              proof.noise_rank)),
            ("in-domain n change",
             (proof.m, proof.n + proof.noise_rank, proof.k,
              proof.noise_rank)),
            ("in-domain k change",
             (proof.m, proof.n, proof.k + proof.noise_rank,
              proof.noise_rank)),
            ("in-domain rank change",
             (proof.m, proof.n, proof.k, 64)),
        ]
        wide_variant_args = [
            ("m wider than u32",
             (proof.m + (1 << 40), proof.n, proof.k, proof.noise_rank)),
            ("n wider than u32",
             (proof.m, proof.n + (1 << 40), proof.k, proof.noise_rank)),
            ("k wider than u32",
             (proof.m, proof.n, proof.k + (1 << 40), proof.noise_rank)),
            ("rank wider than u16",
             (proof.m, proof.n, proof.k, proof.noise_rank + (1 << 16))),
        ]
        # The four valid-width controls are meaningful only if the public
        # binding constructs them and the official verifier rejects them.
        # Counting a constructor/type failure here would falsely claim that
        # the verifier had checked the mutated proof.
        for name, dims in in_domain_variant_args:
            try:
                variant = pm.PlainProof(
                    *dims, proof.a, proof.bt, proof.moe)
                encoded = variant.to_base64()
            except Exception as exc:                     # noqa: BLE001
                return False, "failed to build %s control: %s" % (name, exc)
            tests.append((name, self._verify_with(job, encoded)))

        # The remaining values exceed their consensus wire widths. A secure
        # binding may reject them before serialization; otherwise the official
        # verifier must reject their encoded form.
        for name, dims in wide_variant_args:
            try:
                variant = pm.PlainProof(
                    *dims, proof.a, proof.bt, proof.moe)
                encoded = variant.to_base64()
            except (OverflowError, ValueError) as exc:
                tests.append((name, VerificationResult(
                    VerificationKind.BINDING_REJECTED,
                    "binding rejected value: %s" % exc)))
            except Exception as exc:                     # noqa: BLE001
                return False, "failed to build %s control: %s" % (name, exc)
            else:
                tests.append((name, self._verify_with(
                    job, encoded, allow_decode_rejection=True)))
        if len(tests) != OFFICIAL_NEGATIVE_CONTROLS_PER_PROOF:
            return False, ("mock defines %d official negative controls, "
                           "expected %d" %
                           (len(tests),
                            OFFICIAL_NEGATIVE_CONTROLS_PER_PROOF))
        for name, result in tests:
            if result.kind is VerificationKind.ACCEPTED:
                return False, "official verifier accepted negative control: " + name
            if result.kind is VerificationKind.ERROR:
                return False, "%s control failed: %s" % (name, result.reason)
            if not result.reason:
                return False, "official verifier gave no reason for " + name
        self.official_negative_controls += len(tests)
        self.official_verified += 1
        return True, positive.reason

    # -- the server ------------------------------------------------------
    def serve(self):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", self.port))
        srv.listen(1)
        srv.settimeout(60)
        try:
            conn, _ = srv.accept()
        except socket.timeout:
            self.errors.append("the miner never connected")
            self.done.set()
            srv.close()
            return
        conn.settimeout(180)
        buf = b""
        try:
            while not self.done.is_set():
                try:
                    data = conn.recv(65536)
                except socket.timeout:
                    self.errors.append("timed out waiting for the miner")
                    break
                if not data:
                    break
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    if not line.strip():
                        continue
                    try:
                        msg = json.loads(line)
                    except ValueError:
                        self.errors.append("not JSON on the wire: %r" % line[:200])
                        continue
                    self._handle(conn, msg)
                    if len(self.submits) >= self.total_shares:
                        # Let the miner log the last verdict before the socket
                        # disappears underneath it.
                        time.sleep(1.5)
                        self.done.set()
                        break
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass
        finally:
            self.done.set()
            try:
                conn.close()
            except OSError:
                pass
            srv.close()

    # -- contract checks -------------------------------------------------
    #
    # These are the three failures pearl_pool.h documents as looking like
    # something else at a real pool, so the mock answers them the way the pool
    # does instead of quietly tolerating them.
    def _check_params(self, conn, msg, want):
        p = msg.get("params")
        if not isinstance(p, dict):
            self.violations.append(
                "%s params are %s, not an object - a real gateway answers "
                "code 20 and the miner sees 'Invalid params'"
                % (msg.get("method"), type(p).__name__))
            self._send(conn, {"id": msg.get("id"), "result": None,
                              "error": {"code": 20, "message": "Invalid params"}})
            return None
        for k in want:
            if k not in p:
                self.violations.append("%s params are missing %r (have %s)"
                                       % (msg.get("method"), k, sorted(p)))
                self._send(conn, {"id": msg.get("id"), "result": None,
                                  "error": {"code": 24, "message": "Wallet is missing"}})
                return None
        return p

    def _handle(self, conn, msg):
        method = msg.get("method", "")
        if method == "mining.authorize":
            self.authorize = msg
            p = self._check_params(conn, msg, ("wallet",))
            if p is None:
                return
            w = p.get("wallet", "")
            if not isinstance(w, str) or not w.startswith("prl1"):
                # The real gateway's exact wording, and the reason a user with
                # an Ergo address sees "Invalid Pearl address".
                self.violations.append("wallet %r is not a Pearl address" % w[:12])
                self._send(conn, {"id": msg.get("id"), "result": None,
                                  "error": {"code": 24, "message": "Invalid Pearl address"}})
                return
            if self.unrelated_auth_success_then_reject:
                self._send(conn, {"id": msg.get("id") + 1, "result": True,
                                  "error": None})
                self._send(conn, {"id": msg.get("id"), "result": False,
                                  "error": {"code": 24,
                                            "message": "mock auth rejection"}})
                self.done.set()
                return
            if self.hold_auth_reply_seconds:
                # A peer can keep TCP open and drip an incomplete JSON line.
                # The client's post-connect authorization deadline must still
                # expire.
                conn.sendall(b'{"id":1,"result":')
                self.done.wait(timeout=self.hold_auth_reply_seconds)
                self.done.set()
                return
            if self.notify_before_auth_reply:
                self._notify(conn)
            self._send(conn, {"id": msg.get("id"), "result": True, "error": None})
            if self.bad_notify_first:
                # Right shape, unusable contents: a short header. The miner
                # must refuse it, not mine 76 bytes of whatever follows.
                self._send(conn, {"id": None, "method": "mining.notify",
                                  "params": {"job_id": "mock-bad", "header": "dead",
                                             "target": EASY_TARGET, "height": 1,
                                             "cert_version": 3}})
            if not self.notify_before_auth_reply:
                self._notify(conn)
            return

        if method == "mining.submit":
            if self.first_submit_received_at is None:
                self.first_submit_received_at = time.monotonic()
            p = self._check_params(conn, msg, ("job_id", "plain_proof"))
            if p is None:
                return
            try:
                raw = base64.b64decode(p["plain_proof"], validate=True)
            except Exception:                        # noqa: BLE001
                raw = b""
            if len(raw) < 32:
                self.violations.append(
                    "plain_proof decodes to %d bytes; the pool reads m/n/k/rank "
                    "out of the first 32" % len(raw))
                self._send(conn, {"id": msg.get("id"), "result": None,
                                  "error": {"code": 23, "message": "Invalid proof"}})
                return
            if self.verify_submits:
                verified, reason = self._verify_proof(p)
                if not verified:
                    self.submits.append(msg)
                    self.newest_at_submit.append(self.notified[-1])
                    self.violations.append(
                        "official Pearl verifier rejected submit: %s" % reason)
                    self._send(conn, {"id": msg.get("id"), "result": False,
                                      "error": {"code": 23,
                                                "message": "Invalid proof: " + reason}})
                    return
            idx = len(self.submits)
            self.submits.append(msg)
            # What was newest when this submit was sent, recorded before any
            # push below changes it.
            self.newest_at_submit.append(self.notified[-1])
            for _ in range(self.push_burst):
                current = self.jobs[p["job_id"]]
                self._send(conn, {"id": None, "method": "mining.notify",
                                  "params": current})
            if self.renotify_before_reply:
                self._notify(conn)
            accepted = self.reject_from is None or idx < self.reject_from
            stale = self.stale_reply_text is not None
            if stale:
                self.stale += 1
            elif accepted:
                self.credited += 1
                if self.renotify_before_reply:
                    # Pearl pool contracts define result:true/error:null as an
                    # accept even during retarget grace. Record replacement
                    # timing as a diagnostic subset of credited shares.
                    self.accepted_superseded += 1
            if self.hold_submit_reply_seconds:
                # Keep the connection open with an incomplete correlated reply
                # beyond the client's operation deadline. Closing immediately
                # would only test EOF handling, not the hard deadline.
                conn.sendall(("{\"id\":%s,\"result\":" %
                              msg.get("id")).encode())
                self.done.wait(timeout=self.hold_submit_reply_seconds)
                self.done.set()
                return
            reply_result = False if stale else accepted
            reply = {"id": msg.get("id"), "result": reply_result}
            reply["error"] = None if (accepted or stale) else \
                {"code": 23, "message": "mock rejection, share not counted"}
            if self.stale_reply_text is not None:
                reply["message"] = self.stale_reply_text
            self._send(conn, reply)
            return

        # Anything else is a contract surprise worth failing on.
        self.errors.append("unexpected method %r" % method)
