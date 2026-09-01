from __future__ import annotations

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DocumentationHygieneTests(unittest.TestCase):
    def test_readme_uses_portable_repository_links(self) -> None:
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertNotRegex(readme, r"\]\(/Users/")
        self.assertIn("[CONTRIBUTORS.md](CONTRIBUTORS.md)", readme)
        self.assertIn("[CITATION.cff](CITATION.cff)", readme)

    def test_no_legacy_urg32_names_in_main_docs(self) -> None:
        checked = [
            ROOT / "README.md",
            ROOT / "docs" / "learn" / "tutorial.md",
            ROOT / "docs" / "software" / "framework_manual.md",
        ]
        for path in checked:
            with self.subTest(path=path):
                text = path.read_text(encoding="utf-8")
                self.assertNotIn("urg32", text.lower())

    def test_markdown_headings_are_spaced(self) -> None:
        for path in (ROOT / "docs").rglob("*.md"):
            bad_headings: list[str] = []
            in_fence = False
            for line in path.read_text(encoding="utf-8").splitlines():
                if line.startswith("```"):
                    in_fence = not in_fence
                    continue
                if not in_fence and re.match(r"^#+[^#\s]", line):
                    bad_headings.append(line)
            self.assertEqual(bad_headings, [], f"bad heading spacing in {path}")


if __name__ == "__main__":
    unittest.main()
