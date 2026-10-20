"""The tuning implementation must never request NVML possible-clock enumeration APIs."""

import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FORBIDDEN = re.compile("nvmlDeviceGet" + r"[A-Za-z0-9_]*" + "Supported" + r"[A-Za-z0-9_]*" + "Clocks")


class NvmlTuningSymbolTests(unittest.TestCase):
    def test_source_does_not_reference_supported_clock_queries(self):
        source_root = os.path.join(ROOT, "src")
        extensions = (".h", ".hpp", ".c", ".cc", ".cpp", ".cu")
        matches = []
        for current, _, files in os.walk(source_root):
            for name in files:
                if not name.endswith(extensions):
                    continue
                path = os.path.join(current, name)
                with open(path, encoding="utf-8") as stream:
                    text = stream.read()
                if FORBIDDEN.search(text):
                    matches.append(os.path.relpath(path, ROOT))
        self.assertEqual(matches, [], "forbidden NVML clock-enumeration API referenced in: " + ", ".join(matches))


if __name__ == "__main__":
    unittest.main()
