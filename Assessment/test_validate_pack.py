#!/usr/bin/env python3
"""Mutation tests: coverage audit must fail closed, not just pass good inputs."""
from __future__ import annotations

import csv
import json
import shutil
import tempfile
import unittest
from pathlib import Path

from validate_pack import ValidationError, validate

HERE = Path(__file__).resolve().parent


class PlanAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ("requirements.csv", "test_cases.json"):
            shutil.copy2(HERE / name, self.root / name)

    def edit_csv(self, mutation):
        path = self.root / "requirements.csv"
        with path.open(newline="", encoding="utf-8") as stream:
            source = csv.DictReader(stream)
            headers = source.fieldnames
            rows = list(source)
        mutation(rows)
        with path.open("w", newline="", encoding="utf-8") as stream:
            out = csv.DictWriter(stream, fieldnames=headers)
            out.writeheader()
            out.writerows(rows)

    def edit_json(self, mutation):
        path = self.root / "test_cases.json"
        tests = json.loads(path.read_text(encoding="utf-8"))
        mutation(tests)
        path.write_text(json.dumps(tests, indent=2), encoding="utf-8")

    def test_valid_baseline(self):
        result = validate(self.root)
        self.assertGreaterEqual(result["requirements"], 22)
        self.assertEqual(result["coverage"], "two-way exact")

    def test_missing_inverse_mapping_is_rejected(self):
        self.edit_csv(lambda rows: next(r for r in rows if r["Requirement Number"] == "FUN-08")
                      .update({"Test Cases": "TC02,TC03,TC04"}))
        with self.assertRaisesRegex(ValidationError, "mapping mismatch"):
            validate(self.root)

    def test_ghost_test_is_rejected(self):
        self.edit_csv(lambda rows: next(r for r in rows if r["Requirement Number"] == "FUN-08")
                      .update({"Test Cases": "TC01,TC02,TC03,TC04,TC99"}))
        with self.assertRaisesRegex(ValidationError, "unknown test ID"):
            validate(self.root)

    def test_white_box_expected_result_is_rejected(self):
        self.edit_json(lambda tests: tests[0]["expected"].append("Inspect debugger task list"))
        with self.assertRaisesRegex(ValidationError, "not black-box"):
            validate(self.root)

    def test_unrealistic_duration_is_rejected(self):
        self.edit_json(lambda tests: tests[0].update({"minutes": 200}))
        with self.assertRaisesRegex(ValidationError, "10-20 min"):
            validate(self.root)

    def test_duplicate_test_case_is_rejected(self):
        self.edit_json(lambda tests: tests.append(dict(tests[0])))
        with self.assertRaisesRegex(ValidationError, "duplicate test ID"):
            validate(self.root)

    def test_unverified_pass_claim_is_rejected(self):
        self.edit_csv(lambda rows: rows[0].update({"Implementation/evidence gate": "PASS"}))
        with self.assertRaisesRegex(ValidationError, "physical PASS"):
            validate(self.root)


if __name__ == "__main__":
    unittest.main(verbosity=2)
