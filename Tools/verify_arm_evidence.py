#!/usr/bin/env python3
"""Fail closed on an ARM build report from a different run/source/binary.

A report is only evidence of a local Cortex-M ELF compilation. It is not
proof of a flashed board, acoustic safety or device performance.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from datetime import datetime, timezone, timedelta
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_PROJECT = "261013-Individual_Demonstration_Connectivity"


def timestamp(value: str) -> datetime:
    t = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if t.tzinfo is None:
        raise ValueError("UTC timestamp must contain a timezone")
    return t.astimezone(timezone.utc)


def verify(report_path: Path, project_root: Path, expected_revision: str, started: datetime) -> list[str]:
    errors = []
    try:
        report = json.loads(report_path.read_text(encoding="utf-8-sig"))
        if not isinstance(report, dict):
            raise ValueError("JSON root must be an object")
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        return [f"ARM build report cannot be read: {type(exc).__name__}"]
    if report.get("status") != "ARM_CLEAN_BUILD_PASS":
        errors.append("ARM clean-build status is not PASS")
    if report.get("project") != EXPECTED_PROJECT:
        errors.append("Report names the wrong project")
    if report.get("firmware_commit") != expected_revision:
        errors.append("Build revision does not match the requested source revision")
    conf = report.get("configuration")
    if conf not in ("Debug", "Release"):
        errors.append("Build configuration is missing or unknown")
    if report.get("physical_tests_performed") is not False or report.get("device_flashed") is not False:
        errors.append("ARM source-build report must not claim physical device testing or flashing")
    try:
        completed = timestamp(report["time_utc"])
        if completed < started - timedelta(seconds=3):
            errors.append("Report predates this build invocation")
        if completed > datetime.now(timezone.utc) + timedelta(minutes=5):
            errors.append("Report timestamp is implausibly in the future")
    except (KeyError, ValueError, TypeError):
        errors.append("Report has an invalid completion timestamp")
    try:
        elf = Path(report["executable_path"]).resolve(strict=True)
        root = (project_root / str(conf)).resolve(strict=False)
        if not elf.is_relative_to(root):
            errors.append("ELF is outside the expected G1 configuration output folder")
        if elf.suffix.lower() != ".elf":
            errors.append("Report does not reference an ELF executable")
        with elf.open("rb") as stream:
            header = stream.read(64)
        if len(header) < 64 or header[:4] != b"\x7fELF" or header[18:20] != b"\x28\x00":
            errors.append("Executable is not an ARM ELF")
        if elf.stat().st_size < 4096:
            errors.append("Executable is unexpectedly small")
        if elf.stat().st_mtime < started.timestamp() - 3:
            errors.append("ELF predates this build invocation")
        size = elf.stat().st_size
        digest = hashlib.sha256()
        with elf.open("rb") as stream:
            for block in iter(lambda: stream.read(128 * 1024), b""):
                digest.update(block)
        if report.get("executable_sha256") != digest.hexdigest():
            errors.append("ELF SHA-256 does not match build report")
        if report.get("executable_bytes") != size:
            errors.append("ELF size does not match build report")
    except (OSError, ValueError, KeyError, TypeError):
        errors.append("ELF path or binary could not be validated")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, default=ROOT / "Validation/results/g1_arm_build.json")
    parser.add_argument("--project-root", type=Path, default=ROOT)
    parser.add_argument("--expected-revision", required=True)
    parser.add_argument("--not-before", required=True, help="UTC ISO 8601 time captured before ARM build")
    args = parser.parse_args()
    try:
        started = timestamp(args.not_before)
    except ValueError as exc:
        print(f"FAIL: invalid run-start timestamp: {exc}")
        return 2
    errors = verify(args.report, args.project_root, args.expected_revision, started)
    for error in errors:
        print("FAIL:", error)
    if errors:
        print("ARM report verification BLOCKED. Do not flash based on this report.")
        return 1
    print("PASS: current-run, source-revision and ARM ELF integrity verified; hardware NOT tested.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
