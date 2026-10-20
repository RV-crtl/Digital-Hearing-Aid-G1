"""Mutation tests for repository-wide Markdown links and Windows path collisions."""
import tempfile
import unittest
from pathlib import Path
from check_tree_integrity import check, resolve_link


class TreeIntegrityTests(unittest.TestCase):
    def test_normalize_paths_and_url_encoding(self):
        self.assertEqual(resolve_link("Documentation/a.md", "../Core/Src/main.c#x"), "Core/Src/main.c")
        self.assertEqual(resolve_link("Hardware/README.md", "Original_KiCad/User%20Interface.kicad_sch"), "Hardware/Original_KiCad/User Interface.kicad_sch")
        self.assertEqual(resolve_link("README.md", "https://github.com/foo/bar"), None)
        self.assertEqual(resolve_link("README.md", "#install"), None)
        self.assertEqual(resolve_link("README.md", "../../secret"), "__OUTSIDE_REPOSITORY__")

    def test_malformed_missing_links_fail(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "README.md").write_text("[Missing](gone.md)\n", encoding="utf-8")
            self.assertTrue(any("gone.md" in x for x in check(root, ["README.md"])))

    def test_detect_windows_case_collision(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "README.md").write_text("See [guide](Guide.md)", encoding="utf-8")
            (root / "Guide.md").write_text("# Hi", encoding="utf-8")
            (root / "guide.md").write_text("# hi", encoding="utf-8")
            errors = check(root, ["README.md", "Guide.md", "guide.md"])
            self.assertTrue(any("collision" in x for x in errors))

    def test_relative_link_passes(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "Documentation").mkdir()
            (root / "Documentation" / "README.md").write_text("[main](../README.md)", encoding="utf-8")
            (root / "README.md").write_text("# Main", encoding="utf-8")
            self.assertEqual(check(root, ["README.md", "Documentation/README.md"]), [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
