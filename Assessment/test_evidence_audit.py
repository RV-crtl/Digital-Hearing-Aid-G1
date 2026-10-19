#!/usr/bin/env python3
"""Evidence audit must reject incomplete, misleading physical PASS claims."""
from __future__ import annotations
import csv,shutil,tempfile,unittest
from pathlib import Path
from evidence_audit import audit
HERE=Path(__file__).resolve().parent

class EvidenceAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        for name in ("test_cases.json","EVIDENCE_REGISTER.csv"):
            shutil.copy2(HERE/name,self.root/name)
    def alter(self,fn):
        path=self.root/"EVIDENCE_REGISTER.csv"
        with path.open(newline="",encoding="utf-8") as f:
            r=csv.DictReader(f);headers=r.fieldnames;rows=list(r)
        fn(rows)
        with path.open("w",newline="",encoding="utf-8") as f:
            w=csv.DictWriter(f,fieldnames=headers);w.writeheader();w.writerows(rows)
    def test_empty_register_is_not_a_pass(self):
        result=audit(self.root)
        self.assertEqual(result["physical_evidence_gate"],"BLOCKED")
        self.assertEqual(result["counts"]["NOT_RUN"],13)
    def test_blank_fields_cannot_pass(self):
        self.alter(lambda rows:rows[0].update({"Result (PASS/FAIL/BLOCKED)":"PASS"}))
        result=audit(self.root)
        self.assertNotEqual(result["errors"],[])
        self.assertEqual(result["physical_evidence_gate"],"BLOCKED")
    def test_fabricated_sha_is_rejected(self):
        self.alter(lambda rows:rows[0].update({
            "Result (PASS/FAIL/BLOCKED)":"PASS",
            "Firmware commit SHA":"12345"}))
        self.assertTrue(any("40-digit" in msg for msg in audit(self.root)["errors"]))
    def test_unknown_result_is_rejected(self):
        self.alter(lambda rows:rows[0].update({"Result (PASS/FAIL/BLOCKED)":"COMPLETE"}))
        self.assertTrue(any("invalid status" in msg for msg in audit(self.root)["errors"]))
    def test_missing_case_is_rejected(self):
        path=self.root/"EVIDENCE_REGISTER.csv"
        lines=path.read_text(encoding="utf-8").splitlines()
        path.write_text("\n".join(line for line in lines if not line.startswith("TC13,"))+"\n",encoding="utf-8")
        self.assertTrue(any("Missing evidence" in msg for msg in audit(self.root)["errors"]))
if __name__=="__main__":unittest.main(verbosity=2)
