#!/usr/bin/env python3
"""Socket/GPU-free harness tests. Injected verdicts are NOT proof acceptance."""
import base64
from contextlib import ExitStack
import io
import json
import hashlib
from importlib.machinery import EXTENSION_SUFFIXES, ExtensionFileLoader
from importlib.util import spec_from_loader
import _json
import os
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

import pearl_mock as fixture
import test_pearl_pool_e2e as e2e
import pearl_verifier_identity as provenance


class VerifierProvenanceTests(unittest.TestCase):
    """Fake module metadata exercises identity wiring, not Rust verification."""

    def native(self, directory, name, exports=None):
        path = Path(directory) / (name.replace(".", "_") + EXTENSION_SUFFIXES[0])
        path.write_bytes(b"injected native-module fixture, never loaded")
        module = types.ModuleType(name)
        module.__file__ = str(path)
        module.__spec__ = spec_from_loader(name, ExtensionFileLoader(name, str(path)))
        values = exports or tuple(object() for _ in provenance.REQUIRED_EXPORTS)
        for key, value in zip(provenance.REQUIRED_EXPORTS, values):
            setattr(module, key, value)
        return module, path

    def package(self, directory, native):
        package = types.ModuleType("pearl_mining")
        path = Path(directory) / "__init__.py"
        path.write_bytes(b"from .pearl_mining import *\n")
        package.__file__ = str(path)
        for key in provenance.REQUIRED_EXPORTS:
            setattr(package, key, getattr(native, key))
        return package, path

    def test_direct_native_extension_resolves(self):
        with tempfile.TemporaryDirectory() as tmp:
            module, path = self.native(tmp, "pearl_mining")
            with patch.dict(sys.modules, {"pearl_mining": module}):
                self.assertEqual(provenance.verifier_extension_path(module), str(path.resolve()))

    def test_package_resolves_actual_reexport_provider_not_wrapper_hash(self):
        with tempfile.TemporaryDirectory() as tmp:
            native, path = self.native(tmp, "pearl_mining.pearl_mining")
            package, wrapper = self.package(tmp, native)
            extension_hash = hashlib.sha256(path.read_bytes()).hexdigest()
            wrapper_hash = hashlib.sha256(wrapper.read_bytes()).hexdigest()
            def git_result(command, **_):
                output = "a" * 40 if "rev-parse" in command else ""
                return types.SimpleNamespace(stdout=output, returncode=0)
            with ExitStack() as stack:
                stack.enter_context(patch.dict(sys.modules, {
                    "pearl_mining": package, "pearl_mining.pearl_mining": native}))
                stack.enter_context(patch.dict(os.environ, {
                    "CANARY_PEARL_VERIFIER_SOURCE": tmp,
                    "CANARY_PEARL_VERIFIER_REV": "a" * 40,
                    "CANARY_PEARL_VERIFIER_SHA256": extension_hash}))
                stack.enter_context(patch.object(e2e.subprocess, "run", side_effect=git_result))
                stack.enter_context(patch.object(e2e, "FAILURES", []))
                stack.enter_context(patch("sys.stdout", new_callable=io.StringIO))
                self.assertEqual(provenance.verifier_extension_path(package), str(path.resolve()))
                self.assertTrue(e2e.official_verifier_identity())
                os.environ["CANARY_PEARL_VERIFIER_SHA256"] = wrapper_hash
                self.assertFalse(e2e.official_verifier_identity())
                self.assertIn("module sha256", e2e.FAILURES[-1])

    def test_wrapper_only_and_unrelated_native_export_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            native, _ = self.native(tmp, "pearl_mining.pearl_mining")
            package, _ = self.package(tmp, native)
            with patch.dict(sys.modules, {"pearl_mining": package}):
                with self.assertRaises(ValueError):
                    provenance.verifier_extension_path(package)
            unrelated, _ = self.native(tmp, "pearl_mining.other")
            with patch.dict(sys.modules, {"pearl_mining": package, "pearl_mining.other": unrelated}):
                with self.assertRaises(ValueError):
                    provenance.verifier_extension_path(package)

    def test_ambiguous_native_providers_fail_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            first, _ = self.native(tmp, "pearl_mining.first")
            values = tuple(getattr(first, key) for key in provenance.REQUIRED_EXPORTS)
            second, _ = self.native(tmp, "pearl_mining.second", values)
            package, _ = self.package(tmp, first)
            with patch.dict(sys.modules, {
                    "pearl_mining": package, "pearl_mining.first": first,
                    "pearl_mining.second": second}):
                with self.assertRaises(ValueError):
                    provenance.verifier_extension_path(package)

    def test_real_host_extension_loader_is_supported(self):
        # CPython may statically link _json (Ubuntu 26.04 and standalone
        # builds). A built-in has no hashable extension file and must remain
        # rejected; do not relax the actual Pearl oracle's native-file gate.
        # Exercise a genuinely loaded native module without an official oracle.
        with patch.object(provenance, "REQUIRED_EXPORTS",
                          ("encode_basestring", "scanstring", "make_encoder")):
            if not isinstance(_json.__spec__.loader, ExtensionFileLoader):
                with self.assertRaises(ValueError):
                    provenance.verifier_extension_path(_json)
                return
            self.assertEqual(provenance.verifier_extension_path(_json),
                             os.path.realpath(_json.__file__))


class FakeConnection:
    def __init__(self):
        self.messages = []

    def sendall(self, data):
        self.messages.append(json.loads(data))


class InjectedVerifier:
    """Only a deterministic test double of Python API shapes, not cryptography."""
    dims = (4096, 65536, 4096, 128)

    def __init__(self, job):
        self.job = job
        self.calls = []
        self.verifier_fault = None
        self.decode_fault = None
        self.construct_fault = None
        self.reject_wide_at_binding = False
        self.accept_everything = False
        self.reject_positive = False
        self.positive_return = None
        owner = self

        class Header:
            @staticmethod
            def from_bytes(data):
                if len(data) != 76:
                    raise ValueError("header width")
                return data

        class Proof:
            def __init__(self, m, n, k, rank, a=None, bt=None, moe=None):
                self.m, self.n, self.k, self.noise_rank = m, n, k, rank
                self.a, self.bt, self.moe = a, bt, moe
                dims = (m, n, k, rank)
                if owner.construct_fault and dims != owner.dims:
                    raise owner.construct_fault("injected construction error")
                if owner.reject_wide_at_binding and (
                        max(m, n, k) >= (1 << 32) or rank >= (1 << 16)):
                    raise OverflowError("consensus width")

            def to_base64(self):
                dims = [self.m, self.n, self.k, self.noise_rank]
                # Meet the mock's 32-byte structural precheck without pretending
                # these injected JSON bytes are a Pearl proof serialization.
                return base64.b64encode((json.dumps(dims) + " " * 32).encode()).decode()

            @staticmethod
            def from_base64(encoded):
                dims = tuple(json.loads(base64.b64decode(encoded, validate=True)))
                if owner.decode_fault and dims != owner.dims:
                    raise owner.decode_fault("injected decoder error")
                return Proof(*dims)

        self.IncompleteBlockHeader = Header
        self.PlainProof = Proof

    def verify_plain_proof_for_cert_version(self, cert, header, proof,
                                           nbits_override=None):
        dims = (proof.m, proof.n, proof.k, proof.noise_rank)
        self.calls.append((cert, header, dims, nbits_override))
        positive = (cert == self.job["cert_version"] and
                    header == bytes.fromhex(self.job["header"]) and
                    dims == self.dims and
                    nbits_override == fixture.EASY_SHARE_NBITS)
        if self.verifier_fault and not positive:
            raise self.verifier_fault("injected verifier error")
        if positive and self.positive_return is not None:
            return self.positive_return
        if self.accept_everything:
            return True, "injected accept-all"
        return (True, "injected positive") if positive and not self.reject_positive \
            else (False, "injected negative")


class ContractTests(unittest.TestCase):
    def test_unknown_deadline_excludes_startup_but_counts_post_receipt_time(self):
        # No miner/socket: receipt happens long after hypothetical startup.
        for received, now, deadline_failure in ((1000.0, 1016.0, False),
                                                (1000.0, 1021.0, True),
                                                (None, 1016.0, True)):
            with self.subTest(received=received, now=now), ExitStack() as stack:
                mock = fixture.PearlMock(0)
                mock.first_submit_received_at = received
                mock.submits = [{"id": 101}]
                def observed_run(*args, **kwargs):
                    kwargs["timing"].update(request_id=101, outcome="unknown",
                        first_submit_received_at=received, outcome_observed_at=now,
                        settle_started_at=now, shutdown_requested_at=now + 0.5,
                        process_reaped_at=now + 30, stdout_joined_at=now + 30,
                        server_joined_at=now + 30)
                    return [], ""
                stack.enter_context(patch.object(e2e, "PearlMock", return_value=mock))
                stack.enter_context(patch.object(e2e, "free_port", return_value=0))
                stack.enter_context(patch.object(e2e, "run_case", side_effect=observed_run))
                stack.enter_context(patch.object(e2e.time, "monotonic", return_value=now))
                stack.enter_context(patch.object(e2e, "FAILURES", []))
                stack.enter_context(patch("sys.stdout", new_callable=io.StringIO))
                e2e.phase_unknown_reply("injected-not-a-miner", "no-files")
                errors = [message for message in e2e.FAILURES
                          if "partial open submit reply exceeded" in message or
                          "deadline was not exercised" in message]
                self.assertEqual(bool(errors), deadline_failure)
                self.assertTrue(e2e.FAILURES, "an empty run must never pass")

    def test_canonical_diagnostics_require_complete_exact_binding(self):
        notify = {"schema": "canary-pearl-canonical-diagnostic-v1",
                  "event": "mining.notify", "network": "mainnet",
                  "target_semantics": "base-before-penalty", "cert_version": "v3",
                  "height": 100001, "transport_job_id_hex": b"job-a".hex(),
                  "header_b3": "a" * 16, "raw_target_be": "b" * 64,
                  "resolved_target_be": "c" * 64}
        submit = {**notify, "event": "mining.submit"}
        matches = e2e.canonical_submit_matches_notify
        self.assertTrue(matches(submit, [notify], "job-a", 3))
        self.assertFalse(matches(submit, [], "job-a", 3))
        self.assertFalse(matches(submit, [notify], "job-b", 3))
        self.assertFalse(matches(submit, [notify], "job-a", 2))
        for field in notify:
            incomplete = dict(submit)
            incomplete.pop(field)
            self.assertFalse(matches(incomplete, [notify], "job-a", 3), field)
            mutated = {**notify, field: "different"}
            self.assertFalse(matches(submit, [mutated], "job-a", 3), field)
        for field in ("header_b3", "raw_target_be", "resolved_target_be"):
            # Matching missing/malformed fields on both sides cannot pass.
            self.assertFalse(matches({**submit, field: None},
                                     [{**notify, field: None}], "job-a", 3))
        self.assertFalse(matches({**submit, "height": True}, [notify], "job-a", 3))

    def setUp(self):
        # Guard the entire suite against accidentally turning a unit test into
        # an E2E/miner/socket invocation.
        self.no_socket = patch("socket.socket", side_effect=AssertionError("socket forbidden"))
        self.no_process = patch("subprocess.Popen", side_effect=AssertionError("process forbidden"))
        self.no_socket.start()
        self.no_process.start()
        self.addCleanup(self.no_socket.stop)
        self.addCleanup(self.no_process.stop)

    def prepared(self, cert=3):
        mock = fixture.PearlMock(0, cert_version=cert)
        conn = FakeConnection()
        jid = mock._notify(conn)
        verifier = InjectedVerifier(mock.jobs[jid])
        mock._pearl_mining = verifier
        proof = verifier.PlainProof(*verifier.dims).to_base64()
        return mock, verifier, {"job_id": jid, "plain_proof": proof}, conn

    def assert_failure(self, mock, params, text=None):
        accepted, reason = mock._verify_proof(params)
        self.assertFalse(accepted)
        self.assertTrue(reason)
        if text:
            self.assertIn(text, reason)
        self.assertEqual(mock.official_verified, 0)
        self.assertEqual(mock.official_negative_controls, 0)

    def test_valid_cert_height_defaults_and_real_wire_fields(self):
        for cert, height in ((2, 98001), (3, 100001)):
            with self.subTest(cert=cert):
                mock, _, _, conn = self.prepared(cert)
                params = conn.messages[0]["params"]
                self.assertEqual((params["cert_version"], params["height"]), (cert, height))
                self.assertEqual(params["target"], fixture.EASY_TARGET)
                self.assertEqual(int.from_bytes(bytes.fromhex(params["header"])[72:], "little"),
                                 fixture.HEADER_NBITS)
                self.assertEqual(mock.jobs[params["job_id"]], params)

    def test_invalid_cert_and_height_types(self):
        for cert in (True, False, 0, 1, 4, -1, "2", 2.0, None):
            with self.subTest(cert=cert), self.assertRaises(ValueError):
                fixture.PearlMock(0, cert_version=cert)
        for base in (True, False, "98000", 98000.0, -1, fixture.UINT64_MAX):
            with self.subTest(base=base), self.assertRaises(ValueError):
                fixture.PearlMock(0, cert_version=2, height_base=base)

    def test_height_boundaries(self):
        for cert, height in ((2, 96251), (2, 98999), (3, 99000), (3, fixture.UINT64_MAX)):
            fixture.validate_fixture_height(cert, height)
        for cert, height in ((2, 96250), (2, 99000), (3, 98999), (3, 0),
                             (3, fixture.UINT64_MAX + 1)):
            with self.subTest(cert=cert, height=height), self.assertRaises(ValueError):
                fixture.validate_fixture_height(cert, height)

    def test_notify_boundary_failure_is_transactional(self):
        for cert, last in ((2, 98999), (3, fixture.UINT64_MAX)):
            mock = fixture.PearlMock(0, cert_version=cert, height_base=last - 1)
            conn = FakeConnection()
            mock._notify(conn)
            before = (mock._n, dict(mock.jobs), list(mock.notified), list(conn.messages))
            with self.assertRaises(ValueError):
                mock._notify(conn)
            self.assertEqual(before, (mock._n, mock.jobs, mock.notified, conn.messages))

    def test_both_positive_paths_and_opposite_cert_target_controls(self):
        for cert in (2, 3):
            with self.subTest(cert=cert):
                mock, verifier, params, _ = self.prepared(cert)
                self.assertTrue(mock._verify_proof(params)[0])
                self.assertEqual(mock.official_verified, 1)
                self.assertEqual(mock.official_negative_controls, 14)
                self.assertEqual(verifier.calls[0][0], cert)
                self.assertEqual(verifier.calls[0][3], fixture.EASY_SHARE_NBITS)
                self.assertIn(3 if cert == 2 else 2, [call[0] for call in verifier.calls])
                self.assertIn(None, [call[3] for call in verifier.calls])
                self.assertIn(fixture.IMPOSSIBLE_SHARE_NBITS, [call[3] for call in verifier.calls])
                # All four valid-width mutated configurations reach verify.
                for dims in ((4224, 65536, 4096, 128), (4096, 65664, 4096, 128),
                             (4096, 65536, 4224, 128), (4096, 65536, 4096, 64)):
                    self.assertIn(dims, [call[2] for call in verifier.calls])

    def test_binding_width_rejection_is_explicitly_allowed(self):
        mock, verifier, params, _ = self.prepared()
        verifier.reject_wide_at_binding = True
        self.assertTrue(mock._verify_proof(params)[0])
        self.assertEqual(mock.official_negative_controls, 14)

    def test_positive_rejection_is_not_a_scripted_accept(self):
        mock, verifier, params, conn = self.prepared()
        verifier.reject_positive = True
        mock.verify_submits = True
        mock._handle(conn, {"id": 9, "method": "mining.submit", "params": params})
        self.assertEqual(mock.credited, 0)
        self.assertEqual(mock.official_verified, 0)
        self.assertEqual(len(mock.violations), 1)
        self.assertIs(conn.messages[-1]["result"], False)

    def test_scripted_reject_still_verifies_first(self):
        mock, _, params, conn = self.prepared()
        mock.verify_submits = True
        mock.reject_from = 0
        mock._handle(conn, {"id": 9, "method": "mining.submit", "params": params})
        self.assertEqual(mock.official_verified, 1)
        self.assertEqual(mock.credited, 0)
        self.assertIs(conn.messages[-1]["result"], False)

    def test_accept_everything_double_fails_controls(self):
        mock, verifier, params, _ = self.prepared()
        verifier.accept_everything = True
        self.assert_failure(mock, params, "accepted negative control")

    def test_unexpected_verifier_exceptions_never_count_as_rejection(self):
        for exc in (RuntimeError, TypeError, ValueError, OverflowError, MemoryError):
            with self.subTest(exc=exc):
                mock, verifier, params, _ = self.prepared()
                verifier.verifier_fault = exc
                self.assert_failure(mock, params, "adapter error")

    def test_in_domain_decoder_errors_are_not_verifier_rejections(self):
        for exc in (ValueError, OverflowError, TypeError, RuntimeError):
            with self.subTest(exc=exc):
                mock, verifier, params, _ = self.prepared()
                verifier.decode_fault = exc
                self.assert_failure(mock, params, "adapter error")

    def test_in_domain_constructor_failure_is_fatal(self):
        for exc in (ValueError, OverflowError, TypeError):
            mock, verifier, params, _ = self.prepared()
            verifier.construct_fault = exc
            self.assert_failure(mock, params, "control")

    def test_non_boolean_or_malformed_verifier_results_fail_closed(self):
        for result in ((1, "yes"), ("false", "no"), (None, "no"),
                       (True, None), (False, ""), (True,), (True, "ok", 7)):
            with self.subTest(result=result):
                mock, verifier, params, _ = self.prepared()
                verifier.positive_return = result
                self.assert_failure(mock, params, "adapter error")

    def test_malformed_proof_decode_is_never_positive(self):
        mock, _, params, _ = self.prepared()
        params["plain_proof"] = "not a proof"
        self.assert_failure(mock, params, "adapter error")

    def test_binding_rejection_only_for_designated_decode_controls(self):
        mock, verifier, params, _ = self.prepared()
        job = mock.jobs[params["job_id"]]
        invalid = base64.b64encode(b"invalid-json").decode()
        self.assertIs(mock._verify_with(job, invalid).kind, fixture.VerificationKind.ERROR)
        self.assertIs(mock._verify_with(job, invalid, allow_decode_rejection=True).kind,
                      fixture.VerificationKind.BINDING_REJECTED)
        with patch.object(verifier.PlainProof, "from_base64", side_effect=TypeError("API")):
            self.assertIs(mock._verify_with(job, invalid, allow_decode_rejection=True).kind,
                          fixture.VerificationKind.ERROR)

    def test_unknown_job_and_control_construction_errors_are_contained(self):
        mock, verifier, params, _ = self.prepared()
        self.assert_failure(mock, {**params, "job_id": "unknown"}, "never notified")
        with patch.object(verifier.PlainProof, "from_base64", side_effect=RuntimeError("API")):
            self.assert_failure(mock, params, "adapter error")

    def test_main_requires_identity_then_both_certificates(self):
        phases = ("phase_auth_correlation", "phase_auth_deadline", "phase_reject",
                  "phase_accepted_superseded", "phase_explicit_stale",
                  "phase_unknown_reply", "phase_push_burst")
        with ExitStack() as stack:
            stack.enter_context(patch.object(e2e, "VERIFY_OFFICIAL", True))
            stack.enter_context(patch.object(e2e, "FAILURES", []))
            stack.enter_context(patch.object(e2e.os, "makedirs"))
            stack.enter_context(patch.object(e2e, "prove_mock_discriminates"))
            stack.enter_context(patch.object(e2e, "expected_proof_shape", return_value=(4096, 65536)))
            identity = stack.enter_context(patch.object(e2e, "official_verifier_identity", return_value=True))
            stack.enter_context(patch.object(e2e, "pick_binary", return_value="injected-not-a-miner"))
            stack.enter_context(patch.object(e2e.subprocess, "run", return_value=types.SimpleNamespace(stdout="pearl-pow")))
            for phase in phases:
                stack.enter_context(patch.object(e2e, phase))
            accept = stack.enter_context(patch.object(e2e, "phase_accept"))
            stack.enter_context(patch("sys.stdout", new_callable=io.StringIO))
            self.assertEqual(e2e.main(), 0)
            self.assertEqual([call.kwargs["cert_version"] for call in accept.call_args_list], [2, 3])
            identity.assert_called_once()
            accept.reset_mock()
            identity.return_value = False
            self.assertEqual(e2e.main(), 1)
            accept.assert_not_called()

    def test_superseded_transition_phase_requests_single_attempt_batches(self):
        # No socket/process is allowed: inspect the actual phase launch contract.
        with ExitStack() as stack:
            stack.enter_context(patch.object(e2e, "FAILURES", []))
            stack.enter_context(patch.object(e2e, "free_port", return_value=0))
            run = stack.enter_context(patch.object(e2e, "run_case", return_value=([], "")))
            stack.enter_context(patch("sys.stdout", new_callable=io.StringIO))
            e2e.phase_accepted_superseded("injected-not-a-miner", "no-files")
            self.assertEqual(run.call_args.kwargs.get("extra_args"), ("--batch", "256"))
            self.assertTrue(e2e.FAILURES, "an empty injected run must not pass")

    def test_superseded_phase_semantics_use_correlated_wire_prefix(self):
        # Script only harness inputs: no socket, miner, GPU, or verifier runs.
        for known_outcome, suffix_outcome, expected_failure in (
                ("accepted_superseded", "unknown", None),
                ("accepted", "unknown", "superseded accepts lost their diagnostic"),
                ("rejected", "unknown", "superseded accepts lost their diagnostic"),
                ("accepted_superseded", "accepted", "unobserved wire attempt")):
            with self.subTest(known=known_outcome, suffix=suffix_outcome):
                mock = fixture.PearlMock(0, total_shares=3, renotify_before_reply=True)
                mock.notified = ["job-a", "job-b", "job-c"]
                mock.newest_at_submit = list(mock.notified)
                mock.submits = [{"id":101 + i, "params":{"job_id":job}}
                                for i, job in enumerate(mock.notified)]
                mock.credited = mock.accepted_superseded = 3
                events = []
                outcomes = ["accepted_superseded", known_outcome,
                            "accepted_superseded", suffix_outcome]
                for i, outcome in enumerate(outcomes):
                    events.append({"event":"mining.submit", "request_id":101 + i})
                    events.append({"event":"mining.reply", "request_id":101 + i,
                                   "outcome":outcome,
                                   "accepted":outcome in ("accepted", "accepted_superseded"),
                                   "accepted_superseded":outcome == "accepted_superseded",
                                   "stale":outcome == "stale", "unknown":outcome == "unknown"})
                output = ('{"accepted":3,"unknown":1,"rejected":0}\n'
                          'pool ACCEPTED a share after replacing its job')
                with ExitStack() as stack:
                    stack.enter_context(patch.object(e2e, "FAILURES", []))
                    stack.enter_context(patch.object(e2e, "VERIFY_OFFICIAL", False))
                    stack.enter_context(patch.object(e2e, "free_port", return_value=0))
                    stack.enter_context(patch.object(e2e, "PearlMock", return_value=mock))
                    stack.enter_context(patch.object(e2e, "run_case", return_value=(events, output)))
                    stack.enter_context(patch("sys.stdout", new_callable=io.StringIO))
                    e2e.phase_accepted_superseded("injected-not-a-miner", "no-files")
                    if expected_failure is None:
                        self.assertEqual(e2e.FAILURES, [])
                    else:
                        self.assertTrue(any(expected_failure in message for message in e2e.FAILURES),
                                        e2e.FAILURES)

    def test_accept_phase_selects_certificate_and_distinct_transcript(self):
        for cert in (2, 3):
            with ExitStack() as stack:
                stack.enter_context(patch.object(e2e, "VERIFY_OFFICIAL", True))
                stack.enter_context(patch.object(e2e, "FAILURES", []))
                stack.enter_context(patch.object(e2e, "free_port", return_value=0))
                mock = fixture.PearlMock(0, cert_version=cert)
                factory = stack.enter_context(patch.object(e2e, "PearlMock", return_value=mock))
                run = stack.enter_context(patch.object(e2e, "run_case", return_value=([], "")))
                stack.enter_context(patch("sys.stdout", new_callable=io.StringIO))
                e2e.phase_accept("injected-not-a-miner", "no-files", cert_version=cert)
                self.assertEqual(factory.call_args.kwargs["cert_version"], cert)
                self.assertIs(factory.call_args.kwargs["verify_submits"], True)
                self.assertEqual(run.call_args.args[2],
                                 e2e.os.path.join("no-files", "accept-v%d.jsonl" % cert))
                self.assertTrue(e2e.FAILURES, "an empty injected run must not pass")


if __name__ == "__main__":
    print("Harness contract tests only: no official verifier, GPU or accepted-proof evidence.")
    unittest.main()
