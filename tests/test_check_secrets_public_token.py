import importlib.util
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("check_secrets", ROOT / "tools" / "check_secrets.py")
CHECK_SECRETS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK_SECRETS)


class PublicTokenTests(unittest.TestCase):
    def findings(self, text, stem):
        findings = []
        CHECK_SECRETS.scan_text("fixture.txt", text.encode(), [stem], findings)
        return findings

    def test_exact_public_account_masks_only_that_token(self):
        stem = "bucn" + "azty"
        for token in CHECK_SECRETS.PUBLIC_TOKENS:
            self.assertEqual(self.findings(token, stem), [])

        cases = ["/home/" + stem + "/x", stem + "90x", "x" + stem + "90", stem + "2020x"]
        for text in cases:
            self.assertTrue(self.findings(text, stem), text)


if __name__ == "__main__":
    unittest.main()
