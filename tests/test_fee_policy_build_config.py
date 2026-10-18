import json
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class FeePolicyBuildConfigTests(unittest.TestCase):
    def test_release_preset_does_not_enable_test_policy(self):
        presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        release = next(p for p in presets["configurePresets"] if p["name"] == "production-sm120")
        self.assertNotIn("CANARY_FEE_TEST_POLICY", release.get("cacheVariables", {}))
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('option(CANARY_FEE_TEST_POLICY "Build the explicitly test-only accelerated donation schedule" OFF)', cmake)


if __name__ == "__main__":
    unittest.main()
