#!/usr/bin/env python3
"""Audit the *draft* black-box test plan; never infer a physical hardware PASS.

Uses cross-directional requirement/test traceability rather than trusting the
CSV's Test Cases column. All checks remain active under python -O.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import re
from pathlib import Path


class ValidationError(ValueError):
    """The assessment source is inconsistent or unsafe to submit."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValidationError(message)


def validate(root: Path) -> dict:
    required_cols = {
        "Requirement Number", "Requirement Description",
        "Black-box acceptance evidence", "Test Cases",
        "Implementation/evidence gate",
    }
    with (root / "requirements.csv").open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        require(reader.fieldnames is not None, "requirements.csv is missing headers")
        require(required_cols.issubset(reader.fieldnames),
                f"requirements.csv missing columns: {sorted(required_cols - set(reader.fieldnames))}")
        rows = list(reader)
    tests = json.loads((root / "test_cases.json").read_text(encoding="utf-8"))
    require(isinstance(tests, list), "test_cases.json must contain an array")
    require(len(rows) >= 22 and len(tests) >= 10,
            "Unexpected missing system requirements or test procedures")

    ids = [r["Requirement Number"].strip() for r in rows]
    tids = [t.get("id") for t in tests if isinstance(t, dict)]
    require(all(ids) and len(ids) == len(set(ids)), "Empty or duplicate requirement ID")
    require(len(tids) == len(tests) and all(isinstance(x, str) and x for x in tids)
            and len(tids) == len(set(tids)), "Malformed or duplicate test ID")
    require(all(re.fullmatch(r"TC\d{2}", t) for t in tids), "Test ID must match TCnn")
    by_test = {t["id"]: t for t in tests}
    by_req = set(ids)

    declared = {}
    for row in rows:
        rid = row["Requirement Number"].strip()
        require(all((row[k] or "").strip() for k in required_cols), f"{rid}: incomplete row")
        references = [x.strip() for x in row["Test Cases"].split(",")]
        require(all(references) and len(references) == len(set(references)),
                f"{rid}: empty/duplicate test mappings")
        require(set(references).issubset(by_test), f"{rid}: unknown test ID")
        require(not re.search(r"(^|\W)PASS(\W|$)", row["Implementation/evidence gate"], re.I),
                f"{rid}: physical PASS claim in an unverified source field")
        declared[rid] = set(references)

    observed = {rid: set() for rid in ids}
    total_minutes = 0.0
    forbidden = re.compile(
        r"\b(?:debugger|UART\s+log|task\s+list|register\s+view|xTaskGet\w*|"
        r"inspect\s+the\s+code|read\s+the\s+firmware)\b", re.I)
    for test in tests:
        tid = test["id"]
        require(isinstance(test.get("name"), str) and test["name"].strip(),
                f"{tid}: missing title")
        duration = test.get("minutes")
        require(type(duration) in (int, float) and math.isfinite(duration) and duration > 0,
                f"{tid}: duration must be a positive finite number")
        total_minutes += duration
        references = test.get("requirements")
        require(isinstance(references, list) and bool(references)
                and all(isinstance(rid, str) for rid in references)
                and len(references) == len(set(references)),
                f"{tid}: invalid requirements list")
        require(set(references).issubset(by_req), f"{tid}: unknown requirement")
        for rid in references:
            observed[rid].add(tid)
        for field in ("prerequisites", "gate"):
            require(isinstance(test.get(field), str) and bool(test[field].strip()),
                    f"{tid}: empty {field}")
        for field in ("procedure", "expected"):
            steps = test.get(field)
            require(isinstance(steps, list) and bool(steps)
                    and all(isinstance(step, str) and step.strip() for step in steps),
                    f"{tid}: empty/malformed {field}")
            for step in steps:
                require(not forbidden.search(step),
                        f"{tid}: internal/debugger-based {field} is not black-box testing")

    for rid in ids:
        require(declared[rid] == observed[rid],
                f"{rid}: mapping mismatch: CSV={sorted(declared[rid])}, "
                f"procedures={sorted(observed[rid])}")
    require(10 <= total_minutes <= 20,
            f"rubric recommends 10-20 min; current plan is {total_minutes:g} min")
    require(tests[-1]["id"] == "TC13"
            and "self-protection" in tests[-1]["name"].lower(),
            "The potentially damaging self-protection test must run last")
    return {
        "requirements": len(rows),
        "tests": len(tests),
        "planned_minutes": round(total_minutes, 2),
        "coverage": "two-way exact",
        "physical_results": "NOT VERIFIED",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    try:
        result = validate(args.source)
    except (ValidationError, OSError, json.JSONDecodeError) as exc:
        parser.exit(1, f"FAIL: {exc}\n")
    print("PASS: draft source internally consistent; "
          f"{result['requirements']} requirements, {result['tests']} procedures, "
          f"{result['planned_minutes']:g} minutes. "
          "Physical results are NOT VERIFIED.")


if __name__ == "__main__":
    main()
