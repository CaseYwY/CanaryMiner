"""Executable test helpers must remain safe for unittest discovery to import."""
import contextlib
import decimal
import importlib.util
import io
from pathlib import Path
import sys
import unittest
from unittest import mock


class AuxiliaryImports(unittest.TestCase):
    def test_helpers_have_no_import_time_work(self):
        for name in ("test_electricity_oracle", "test_credit_worker_boundary", "test_manual_market_terminal"):
            with self.subTest(module=name):
                spec = importlib.util.spec_from_file_location("discovery_fixture", Path(__file__).with_name(name + ".py"))
                module = importlib.util.module_from_spec(spec)
                output = io.StringIO()
                before = decimal.getcontext().prec
                with mock.patch.object(sys, "argv", ["unittest", "discover"]), mock.patch("subprocess.run", side_effect=AssertionError("spawn during import")), contextlib.redirect_stdout(output):
                    spec.loader.exec_module(module)
                self.assertEqual(output.getvalue(), "")
                self.assertEqual(decimal.getcontext().prec, before)


if __name__ == "__main__":
    unittest.main()
