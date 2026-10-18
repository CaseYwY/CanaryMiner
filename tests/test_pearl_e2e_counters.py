#!/usr/bin/env python3
"""CPU-only telemetry parser checks; no miner, sockets or verifier invoked."""
import json
import unittest

from test_pearl_pool_e2e import reported_accepted


class ReportedCountersTests(unittest.TestCase):
    def test_current_telemetry_intervening_fields(self):
        line = ('{"accepted":14,"pool_rejected":2,"stale":3,"unknown":4,'
                '"pending":0,"accepted_superseded":1,"host_invalid":5,'
                '"submit_failed":6,"rejected":13,"nonces":22}')
        self.assertEqual(reported_accepted(line), (14, 13))

    def test_reordered_whitespace_crlf_and_latest_complete_pair(self):
        text = ('ordinary startup\n{"accepted":1,"rejected":0}\r\n'
                ' { "rejected" : 4, "rate": 3.25, "accepted" : 8 } \r\n'
                '{"accepted":999}\n{"message":"not counters"}')
        self.assertEqual(reported_accepted(text), (8, 4))

    def test_json_precedence_over_legacy_is_preserved(self):
        text = ('accepted=20 rejected=30\n{"accepted":1,"rejected":2}\n'
                'accepted=90 rejected=99')
        self.assertEqual(reported_accepted(text), (1, 2))

    def test_zero_and_large_integer_counts(self):
        self.assertEqual(reported_accepted('{"accepted":0,"rejected":0}'), (0, 0))
        count = (1 << 64) - 1
        self.assertEqual(reported_accepted(json.dumps({"accepted":count, "rejected":0})),
                         (count, 0))

    def test_invalid_json_counter_types_and_negative_values(self):
        for field in ("accepted", "rejected"):
            for value in (True, False, -1, 1.0, "1", None, [], {}):
                with self.subTest(field=field, value=value):
                    record = {"accepted":1, "rejected":2}
                    record[field] = value
                    self.assertEqual(reported_accepted(json.dumps(record)), (None, None))

    def test_no_cross_record_pair_or_nested_counter_lookup(self):
        for text in ('{"accepted":1}\n{"rejected":2}',
                     '{"nested":{"accepted":1,"rejected":2}}',
                     '[{"accepted":1,"rejected":2}]',
                     '{"accepted":1,"pool_rejected":2}',
                     '{"accepted_superseded":1,"rejected":2}'):
            with self.subTest(text=text):
                self.assertEqual(reported_accepted(text), (None, None))

    def test_malformed_duplicate_or_json_embedded_text_not_fallback(self):
        for text in ('{"accepted":1,"rejected":2',
                     '{"accepted":1,"accepted":9,"rejected":2}',
                     '{"accepted":1,"rejected":2,"rejected":3}',
                     '{"accepted":-1,"rejected":2,"message":"accepted=9 rejected=9"}',
                     '"accepted=9 rejected=9"',
                     '{broken accepted=9 rejected=9'):
            with self.subTest(text=text):
                self.assertEqual(reported_accepted(text), (None, None))

    def test_legacy_text_supported_and_latest_pair_used(self):
        self.assertEqual(reported_accepted('accepted=1 rejected=2\nACCEPTED: 4, REJECTED: 5'),
                         (4, 5))
        self.assertEqual(reported_accepted('accepted 0 rejected 0'), (0, 0))

    def test_legacy_invalid_numeric_forms_and_other_fields(self):
        for text in ('accepted=-1 rejected=2', 'accepted=1 rejected=-2',
                     'accepted=1.5 rejected=2', 'accepted=1 rejected=2.5',
                     'accepted=true rejected=2', 'accepted=1 rejected=false',
                     'accepted=1e2 rejected=3', 'accepted=1 rejected=2e3',
                     'accepted_superseded=1 rejected=2',
                     'accepted=1 pool_rejected=2'):
            with self.subTest(text=text):
                self.assertEqual(reported_accepted(text), (None, None))

    def test_empty_and_noise(self):
        for text in ('', ' \n\r\n', 'miner started\nheartbeat success',
                     'null\ntrue\n17'):
            self.assertEqual(reported_accepted(text), (None, None))


if __name__ == "__main__":
    unittest.main()
