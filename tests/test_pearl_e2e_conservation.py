"""CPU-only checks of transcript/wire accounting; no GPU or network."""
import contextlib
import io
import types
import unittest
from unittest.mock import patch

import test_pearl_pool_e2e as gate


def attempt(request_id):
    return {"event": "mining.submit", "request_id": request_id}


def reply(request_id, outcome):
    return {"event": "mining.reply", "request_id": request_id,
            "outcome": outcome,
            "accepted": outcome in ("accepted", "accepted_superseded"),
            "accepted_superseded": outcome == "accepted_superseded",
            "stale": outcome == "stale", "unknown": outcome == "unknown"}


class ConservationTests(unittest.TestCase):
    def check_case(self, events, wire, valid):
        gate.FAILURES.clear()
        with contextlib.redirect_stdout(io.StringIO()):
            gate.reply_conservation(events, [{"id": value} for value in wire],
                                    "fixture")
        self.assertEqual(not gate.FAILURES, valid, gate.FAILURES)

    def test_observed_accept_then_unobserved_unknown(self):
        self.check_case([attempt(101), reply(101, "accepted"),
                         attempt(102), reply(102, "unknown")], [101], True)

    def test_all_observed(self):
        for outcome in ("accepted", "accepted_superseded", "rejected",
                        "stale", "unknown"):
            with self.subTest(outcome=outcome):
                self.check_case([attempt(1), reply(1, outcome)], [1], True)

    def test_unobserved_verdict_cannot_be_invented(self):
        for outcome in ("accepted", "accepted_superseded", "rejected", "stale"):
            with self.subTest(outcome=outcome):
                self.check_case([attempt(1), reply(1, "accepted"),
                                 attempt(2), reply(2, outcome)], [1], False)

    def test_count_only_cannot_hide_mismatched_reply(self):
        self.check_case([attempt(1), reply(2, "accepted")], [1], False)

    def test_duplicate_attempt_or_reply(self):
        self.check_case([attempt(1), attempt(1), reply(1, "accepted"),
                         reply(1, "unknown")], [1], False)
        self.check_case([attempt(1), reply(1, "accepted"),
                         reply(1, "accepted")], [1], False)

    def test_missing_attempt_reply_or_wire_prefix(self):
        self.check_case([reply(1, "accepted")], [1], False)
        self.check_case([attempt(1)], [1], False)
        self.check_case([attempt(1), reply(1, "accepted")], [2], False)
        self.check_case([attempt(1), reply(1, "unknown"), attempt(2),
                         reply(2, "accepted")], [2], False)

    def test_invalid_id_or_outcome(self):
        for value in (None, True, 0, -1, "1"):
            with self.subTest(value=value):
                self.check_case([attempt(value), reply(value, "accepted")],
                                [value], False)
        self.check_case([attempt(1), reply(1, "invented")], [1], False)

    def test_causal_order_and_verdict_flags(self):
        self.check_case([reply(1, "accepted"), attempt(1)], [1], False)
        contradictory = reply(2, "unknown")
        contradictory["accepted"] = True
        self.check_case([attempt(1), reply(1, "accepted"), attempt(2),
                         contradictory], [1], False)
        missing = reply(1, "accepted")
        del missing["unknown"]
        self.check_case([attempt(1), missing], [1], False)

    def test_duplicate_long_wire_prefix_or_reversed_replies(self):
        self.check_case([attempt(1), reply(1, "accepted")], [1, 1], False)
        self.check_case([attempt(1), reply(1, "accepted")], [1, 2], False)
        self.check_case([attempt(1), attempt(2), reply(2, "accepted"),
                         reply(1, "unknown")], [1, 2], False)


class UnknownPhaseTests(unittest.TestCase):
    def phase_case(self, events, wire, valid):
        # Exercise the actual phase and shared conservation implementation;
        # only process/network execution, clock and mock data are injected.
        mock = types.SimpleNamespace(
            errors=[], violations=[], submits=[{"id":value} for value in wire],
            first_submit_received_at=100.0 if wire else None)
        output = ('{"uptime_s":20,"submitted":1,"acknowledged":0,"accepted":0,'
                  '"pool_rejected":0,"stale":0,"pending":0,"unknown":1,'
                  '"accepted_superseded":0,"host_invalid":0,"submit_failed":0,"rejected":0}\n'
                  'share outcome UNKNOWN')
        def observed_run(*args, **kwargs):
            kwargs["timing"].update(request_id=101, outcome="unknown",
                first_submit_received_at=mock.first_submit_received_at,
                outcome_observed_at=115.0, settle_started_at=115.0,
                shutdown_requested_at=115.5, process_reaped_at=130.0,
                stdout_joined_at=130.0, server_joined_at=130.0)
            return events, output
        with contextlib.ExitStack() as stack:
            stack.enter_context(patch.object(gate, "FAILURES", []))
            stack.enter_context(patch.object(gate, "VERIFY_OFFICIAL", False))
            stack.enter_context(patch.object(gate, "free_port", return_value=0))
            stack.enter_context(patch.object(gate, "PearlMock", return_value=mock))
            stack.enter_context(patch.object(gate.time, "monotonic", return_value=115.0))
            run = stack.enter_context(patch.object(gate, "run_case", side_effect=observed_run))
            stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
            gate.phase_unknown_reply("injected-not-a-miner", "no-files")
            self.assertEqual(not gate.FAILURES, valid, gate.FAILURES)
            self.assertEqual(run.call_args.kwargs["stop_reply_outcome"], "unknown")

    def test_exact_observed_unknown_passes_but_unseen_suffix_is_rejected(self):
        self.phase_case([attempt(101), reply(101, "unknown")], [101], True)
        self.phase_case([attempt(101), reply(101, "unknown"),
                         attempt(102), reply(102, "unknown")], [101], False)

    def test_observed_wrong_outcome_is_rejected(self):
        for outcome in ("accepted", "accepted_superseded", "rejected", "stale"):
            with self.subTest(outcome=outcome):
                self.phase_case([attempt(101), reply(101, outcome),
                                 attempt(102), reply(102, "unknown")], [101], False)

    def test_duplicate_or_missing_terminal_is_rejected(self):
        for events in (
                [attempt(101), reply(101, "unknown"), reply(101, "unknown")],
                [attempt(101)],
                [attempt(101), reply(101, "unknown"), attempt(102)],
                [attempt(101), attempt(102), reply(102, "unknown")]):
            with self.subTest(events=events):
                self.phase_case(events, [101], False)

    def test_no_wire_submit_is_rejected(self):
        self.phase_case([], [], False)
        self.phase_case([attempt(101), reply(101, "unknown")], [], False)


if __name__ == "__main__":
    unittest.main()
