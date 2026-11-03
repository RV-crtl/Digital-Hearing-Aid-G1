#!/usr/bin/env python3
"""Check archived G11 requirement/test traceability, not current G1 board results.

Current custom-PCB requirements are independently checked by
Tools/check_current_docs.py; do NOT mix legacy BB test IDs with current T1–T7.
"""
import csv
import pathlib
import re
root=pathlib.Path(__file__).resolve().parents[2]
with (root/"Documentation/REQUIREMENTS_EVIDENCE.csv").open(newline="",encoding="utf8") as f:
    ids=[r["requirement_id"] for r in csv.DictReader(f)]
coverage=(root/"Documentation/HD_REQUIREMENTS_COVERAGE.md").read_text(encoding="utf8")
plan=(root/"Documentation/Legacy/G11_BLACK_BOX_TEST_PLAN.md").read_text(encoding="utf8")
mapping={}
for line in coverage.splitlines():
    if not line.startswith("|"): continue
    parts=[part.strip() for part in line.split("|")[1:-1]]
    if len(parts)>2 and parts[0] in ids:
        mapping[parts[0]]=re.findall(r"BB\d\d",parts[2])
missing=set(ids)-set(mapping)
assert not missing, f"Missing requirements from plan: {sorted(missing)}"
assert all(mapping.values()), "At least one requirement lacks a proposed test"
planned=set(re.findall(r"^### (BB\d\d)\b",plan,re.MULTILINE))
referenced={test for tests in mapping.values() for test in tests}
assert referenced<=planned, f"Undefined test IDs: {sorted(referenced-planned)}"
assert len(planned)>=8,"At least eight externally observable test cases required in this draft"
assert plan.count("**Procedure:**")>=len(planned),"Test procedures missing"
assert plan.count("**Expected:**")>=len(planned),"Expected results missing"
assert "BLOCKED" in plan,"Pending physical gates must not be silently hidden"
print(f"LEGACY G11 TRACEABILITY PASS: {len(ids)} historical requirements, {len(planned)} archived black-box cases.")
print("This checks only completeness of the plan. No hardware PASS is inferred.")
