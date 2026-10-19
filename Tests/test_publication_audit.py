"""Mutation regressions for public-source hygiene checks (not a security certification)."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Tools"))
from publication_audit import scan_text


class PublicationSourceTests(unittest.TestCase):
    def test_safe_normal_source(self):
        self.assertEqual(scan_text("Core/Src/foo.c", "const char *label=\"STATUS\";"), [])

    def test_github_token_flagged_without_echoing_secret(self):
        token = "ghp_" + "a" * 36
        findings = scan_text("tmp.txt", "token=" + token)
        self.assertEqual(findings, ["tmp.txt: github_token"])
        self.assertNotIn(token, str(findings))

    def test_aws_and_private_key_flagged(self):
        key = "AKIA" + "A" * 16
        self.assertIn("creds.txt: aws_access_key", scan_text("creds.txt", key))
        start = "-" * 5 + "BEGIN " + "PRIVATE KEY" + "-" * 5
        self.assertIn("keys.txt: private_key_header", scan_text("keys.txt", start))

    def test_partial_example_token_not_a_secret(self):
        self.assertEqual(scan_text("docs.md", "ghp_EXAMPLE"), [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
