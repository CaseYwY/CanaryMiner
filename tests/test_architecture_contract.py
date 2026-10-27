"""Documentation traceability only; this does not certify runtime feature correctness."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / "docs" / "architecture"


class ArchitectureContract(unittest.TestCase):
    def test_requirements_have_modules_tasks_tests_and_status(self):
        rows = [line for line in (BOOK / "requirements.md").read_text(encoding="utf8").splitlines() if line.startswith("| R-")]
        self.assertGreaterEqual(len(rows), 50)
        seen = set()
        modules = (BOOK / "system.md").read_text(encoding="utf8")
        tasks = (BOOK / "execution-plan.md").read_text(encoding="utf8")
        tests = (BOOK / "testing.md").read_text(encoding="utf8")
        for row in rows:
            fields = [part.strip() for part in row.split("|")[1:-1]]
            self.assertEqual(len(fields), 6, row)
            identity, description, module, task, families, status = fields
            self.assertNotIn(identity, seen)
            seen.add(identity)
            self.assertTrue(description)
            self.assertIn("### " + module + "\n", modules)
            self.assertIn("| " + task + " |", tasks)
            for family in families.split(", "):
                self.assertIn("## " + family + "\n", tests)
            self.assertIn(status, {"DONE", "PARTIAL", "PLANNED", "BLOCKED"})

    def test_local_links_and_code_fences(self):
        for document in BOOK.glob("*.md"):
            text = document.read_text(encoding="utf8")
            self.assertEqual(len(re.findall(r"(?m)^```", text)) % 2, 0, document.name)
            for target in re.findall(r"\]\(([^)]+)\)", text):
                if target.startswith(("https:", "http:", "#")):
                    continue
                self.assertTrue((document.parent / target.split("#", 1)[0]).exists(), f"{document.name}: {target}")

    def test_final_valuation_and_locale_scope(self):
        requirements = (BOOK / "requirements.md").read_text(encoding="utf8")
        row = next(line for line in requirements.splitlines() if line.startswith("| R-INC-005 |"))
        self.assertIn("latest-price", row)
        self.assertNotIn("lock each credit", row)
        formulas = (BOOK / "data-and-calculations.md").read_text(encoding="utf8")
        self.assertIn("production_value_usdt_now = actual_tokens * token_price_usdt_now", formulas)
        self.assertIn("Sale status is **unknown**", formulas)
        terminal = (BOOK / "terminal-experience.md").read_text(encoding="utf8")
        locale_line = next(line for line in terminal.splitlines() if line.startswith("First milestone locale IDs:"))
        self.assertEqual(len(re.findall(r"`([^`]+)`", locale_line)), 16)

    def test_research_references_and_tariff_cases(self):
        research = (ROOT / "docs" / "research" / "international-electricity-billing.md").read_text(encoding="utf8")
        definitions = re.findall(r"(?m)^\[\^([^]]+)\]:", research)
        self.assertEqual(len(definitions), 29)
        self.assertEqual(len(set(definitions)), len(definitions))
        self.assertTrue(set(re.findall(r"\[\^([^]]+)\]", research)) <= set(definitions))
        for case in range(1, 35):
            self.assertIn(f"| T{case:02d} —", research)


if __name__ == "__main__":
    unittest.main()
