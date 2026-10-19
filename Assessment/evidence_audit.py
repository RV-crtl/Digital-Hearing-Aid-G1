#!/usr/bin/env python3
"""No-phantom-PASS audit of the external, signed physical evidence register."""
from __future__ import annotations
import argparse,csv,json,re,sys
from pathlib import Path
from validate_pack import validate
ROOT=Path(__file__).resolve().parent
SHA="Firmware commit SHA"
FIELDS=("Date/Time","Tester","Board/PCB revision",SHA,"Power source",
        "External input stimulus","Instrument/model/calibration",
        "Measured output / observation","Acceptance threshold",
        "Raw evidence path","Supervisor/group sign-off")
STATUS="Result (PASS/FAIL/BLOCKED)"

def audit(root):
    plan=json.loads((root/"test_cases.json").read_text(encoding="utf-8"))
    expected={t["id"]:set(t["requirements"]) for t in plan}
    with (root/"EVIDENCE_REGISTER.csv").open(newline="",encoding="utf-8-sig") as f:
        reader=csv.DictReader(f)
        if reader.fieldnames is None or not set(("Test Case","Requirement(s)",STATUS,*FIELDS)).issubset(reader.fieldnames):
            raise ValueError("Incomplete evidence-register column headers")
        rows=list(reader)
    errors=[]; seen=set(); counts={"PASS":0,"FAIL":0,"BLOCKED":0,"NOT_RUN":0}
    for row in rows:
        tid=(row["Test Case"] or "").strip()
        if tid in seen or tid not in expected:
            errors.append("Duplicate or unknown evidence test: "+tid);continue
        seen.add(tid)
        state=(row[STATUS] or "").strip().upper() or "NOT_RUN"
        if state not in counts:
            errors.append(f"{tid}: invalid status {state}");continue
        counts[state]+=1
        provided={x.strip() for x in (row["Requirement(s)"] or "").split(",") if x.strip()}
        if provided and provided!=expected[tid]:
            errors.append(f"{tid}: evidence requirement IDs differ from procedure")
        if state=="PASS":
            for k in FIELDS:
                if not (row[k] or "").strip(): errors.append(f"{tid}: unsubstantiated PASS without {k}")
            if not re.fullmatch(r"[0-9a-fA-F]{40}",(row[SHA] or "").strip()):
                errors.append(f"{tid}: PASS must identify full 40-digit firmware SHA")
            if provided!=expected[tid]:
                errors.append(f"{tid}: PASS lacks exact requirement mapping")
        if state=="FAIL" and not (row["Measured output / observation"] or "").strip():
            errors.append(f"{tid}: FAIL has no observed defect recorded")
    if seen!=set(expected):errors.append("Missing evidence test rows: "+str(sorted(set(expected)-seen)))
    return {"physical_evidence_gate":"PASS" if not errors and counts["PASS"]==len(expected) else "BLOCKED",
            "counts":counts,"expected_tests":len(expected),"errors":errors,
            "warning":"Source checks cannot validate physical audio, power or mechanics."}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source",type=Path,default=ROOT)
    p.add_argument("--strict",action="store_true")
    p.add_argument("--output",type=Path,default=ROOT.parent/"Validation/results/g1_evidence_audit.json")
    args=p.parse_args()
    try:
        validate(args.source);result=audit(args.source)
    except (ValueError,OSError,json.JSONDecodeError) as e:
        print("FAIL:",e);return 1
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print("PHYSICAL EVIDENCE",result["physical_evidence_gate"],result["counts"])
    for error in result["errors"]: print("FAIL:",error)
    if result["errors"]:return 1
    if args.strict and result["physical_evidence_gate"]!="PASS":
        print("RELEASE BLOCKED: missing measured and signed results");return 2
    return 0
if __name__=="__main__":sys.exit(main())
