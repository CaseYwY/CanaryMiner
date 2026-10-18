"""Synthetic CPU-only timing/accounting regressions; no miner/socket/oracle."""
import copy
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import test_pearl_pool_e2e as gate


def events():
    return [{"event": "mining.submit", "request_id": 101},
            {"event": "mining.reply", "request_id": 101, "outcome": "unknown",
             "accepted": False, "stale": False, "unknown": True, "accepted_superseded": False}]


def output(local=0):
    counters = {"uptime_s": 20, "submitted": 1, "acknowledged": 0, "accepted": 0,
        "pool_rejected": 0, "stale": 0, "unknown": 1, "pending": 0,
        "accepted_superseded": 0, "host_invalid": 0, "submit_failed": local, "rejected": local}
    message = {"event": "error", "msg": "submission failed before acknowledgement  nonce=fixture  "
               "partial pre-submit message; proof not sent"}
    return (json.dumps(message) + "\n") * local + json.dumps(counters)


class UnknownDeadlineTests(unittest.TestCase):
    def timed_run(self, observed_id=101, observation=24.9, late_only=False):
        clock = [0.0]
        server, drain = Mock(), Mock()
        server.is_alive.return_value = False
        server.join.side_effect = lambda **_: clock.__setitem__(0, clock[0] + 2)
        drain.join.side_effect = lambda **_: clock.__setitem__(0, clock[0] + 2)
        mock = SimpleNamespace(serve=Mock(), port=0, submits=[{"id": 101}],
                               first_submit_received_at=10.0, done=Mock())
        mock.done.wait.return_value = True  # Wrong/no event exits without a stamp.
        process = Mock()
        process.wait.side_effect = lambda **_: clock.__setitem__(0, clock[0] + 30)
        with tempfile.TemporaryDirectory() as directory:
            transcript = Path(directory) / "synthetic.jsonl"
            record = {"event": "mining.reply", "request_id": observed_id, "outcome": "unknown"}
            def launch(*_args, **_kwargs):
                clock[0] = observation
                transcript.write_text("" if late_only else json.dumps(record) + "\n")
                return process
            def terminate():
                if late_only:
                    transcript.write_text(json.dumps(record) + "\n")
            process.terminate.side_effect = terminate
            timing = {}
            with patch.object(gate.threading, "Thread", side_effect=[server, drain]), \
                 patch.object(gate.subprocess, "Popen", side_effect=launch), \
                 patch.object(gate.time, "monotonic", side_effect=lambda: clock[0]), \
                 patch.object(gate.time, "sleep", side_effect=lambda seconds: clock.__setitem__(0, clock[0] + seconds)):
                final_events, _ = gate.run_case("NEVER-EXECUTED", mock, str(transcript),
                    stop_reply_outcome="unknown", settle_seconds=0.5, timing=timing)
            sidecar = json.loads(Path(str(transcript) + ".harness-timing.json").read_text())
            self.assertEqual(sidecar, {"schema": "pearl-e2e-operation-observation-v1", **timing})
        return timing, mock, final_events

    def test_slow_cleanup_not_charged_to_operation(self):
        timing, mock, _ = self.timed_run()
        self.assertAlmostEqual(gate.unknown_reply_elapsed(timing, mock), 14.9)
        self.assertGreater(timing["server_joined_at"] - mock.first_submit_received_at, 40)
        self.assertEqual(timing["settle_started_at"], timing["outcome_observed_at"])
        self.assertEqual(timing["process_reaped_at"] - timing["shutdown_requested_at"], 30)

    def test_late_operation_never_excused_by_separate_cleanup(self):
        timing, mock, _ = self.timed_run(observation=30.0)
        self.assertEqual(gate.unknown_reply_elapsed(timing, mock), 20.0)
        self.assertFalse(gate.unknown_reply_elapsed(timing, mock) < 20)

    def test_wrong_or_cleanup_only_reply_never_stamps_deadline(self):
        for kwargs in ({"observed_id": 102}, {"observed_id": True}, {"late_only": True}):
            with self.subTest(kwargs=kwargs):
                timing, mock, final_events = self.timed_run(**kwargs)
                self.assertNotIn("outcome_observed_at", timing)
                self.assertTrue(final_events)  # Final parsing must not retroactively stamp it.
                with self.assertRaises(ValueError): gate.unknown_reply_elapsed(timing, mock)

    def test_timing_types_and_order_fail_closed(self):
        original, mock, _ = self.timed_run()
        for key in original:
            for value in (None, True, -1, float("nan"), float("inf")):
                changed = dict(original); changed[key] = value
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    gate.unknown_reply_elapsed(changed, mock)
        for value in (True, 0, -1, "101"):
            changed_mock = copy.copy(mock); changed_mock.submits = [{"id": value}]
            with self.assertRaises(ValueError): gate.unknown_reply_elapsed(original, changed_mock)
        for value in (None, True, float("nan"), float("inf")):
            changed_mock = copy.copy(mock); changed_mock.first_submit_received_at = value
            with self.assertRaises(ValueError): gate.unknown_reply_elapsed(original, changed_mock)

    def test_remote_and_local_failure_counters_both_validated(self):
        for local in (0, 1):
            result = gate.unknown_reply_counters(events(), output(local))
            self.assertEqual(result["rejected"], local)
            self.assertEqual(result["pool_rejected"], 0)
            self.assertEqual(result["unknown"], 1)

    def test_every_required_counter_and_aggregate_mismatch_rejected(self):
        for local in (0, 1):
            text = output(local)
            lines = text.splitlines()
            original = json.loads(lines[-1])
            for key in set(original) - {"uptime_s"}:
                for bad in (None, True, -1, "0", 0.0, 1 << 64, original[key] + 1):
                    record = dict(original); record[key] = bad
                    with self.subTest(local=local, key=key, bad=bad), self.assertRaises(ValueError):
                        gate.unknown_reply_counters(events(), "\n".join(lines[:-1] + [json.dumps(record)]))
                record = dict(original); del record[key]
                with self.assertRaises(ValueError):
                    gate.unknown_reply_counters(events(), "\n".join(lines[:-1] + [json.dumps(record)]))
        for bad in ("", output(2), output(1).splitlines()[-1],
                    output(1).replace("partial pre-submit message", "unrelated error"),
                    output().replace('"unknown": 1', '"unknown": 1, "unknown": 1')):
            with self.assertRaises(ValueError): gate.unknown_reply_counters(events(), bad)
        with self.assertRaises(ValueError): gate.unknown_reply_counters(events() + events(), output())

    def test_extra_unknown_suffix_rejected_even_with_matching_aggregates(self):
        extra = copy.deepcopy(events())
        for event in extra: event["request_id"] = 102
        record = json.loads(output())
        record.update(submitted=2, unknown=2)
        with self.assertRaises(ValueError):
            gate.unknown_reply_counters(events() + extra, json.dumps(record))


if __name__ == "__main__":
    unittest.main()
