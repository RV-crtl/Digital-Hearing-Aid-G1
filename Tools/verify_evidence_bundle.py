#!/usr/bin/env python3
"""Check a G1 quality ZIP's manifest, SHA-256 evidence and safe member paths.

Never unzip or execute contents. An intact source-evidence ZIP is not an
independent physical hearing-aid test.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import sys
import zipfile
from pathlib import PurePosixPath, Path

MANIFEST = "G1_BUNDLE_MANIFEST.json"
REPO = "RV-crtl/Digital-Hearing-Aid-G1"


def verify(path: Path) -> list[str]:
    errors = []
    try:
        with zipfile.ZipFile(path) as archive:
            names = archive.namelist()
            if len(names) != len(set(names)):
                errors.append("Duplicate archive filenames")
            for name in names:
                normalized = name.replace("\\", "/")
                parts = PurePosixPath(normalized).parts
                if name.startswith(("/", "\\")) or ".." in parts or ":" in normalized:
                    errors.append(f"Unsafe archive member: {name}")
            if names.count(MANIFEST) != 1:
                return errors + ["Manifest must occur exactly once at ZIP root"]
            try:
                manifest = json.loads(archive.read(MANIFEST).decode("utf-8-sig"))
            except (UnicodeError, ValueError):
                return errors + ["Manifest is not valid UTF-8 JSON"]
            if not isinstance(manifest, dict) or manifest.get("repository") != REPO:
                errors.append("Manifest does not identify the G1 repository")
                return errors
            if manifest.get("physical_hardware_verified_by_this_script") is not False:
                errors.append("Software source bundle falsely claims physical verification")
            if manifest.get("arm_compiled_by_this_script") is True and manifest.get("arm_evidence_cryptographically_verified") is not True:
                errors.append("ARM build claim lacks cryptographic-verification flag")
            artifacts = manifest.get("artifacts")
            if not isinstance(artifacts, list):
                return errors + ["Manifest artifacts must be an array"]
            expected = set()
            for entry in artifacts:
                if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
                    errors.append("Malformed manifest artifact entry")
                    continue
                name = entry["path"].replace("\\", "/")
                if name in expected:
                    errors.append(f"Duplicate manifest artifact: {name}")
                expected.add(name)
                if name not in names:
                    errors.append(f"Missing ZIP artifact: {name}")
                    continue
                data_hash = hashlib.sha256(archive.read(name)).hexdigest()
                if entry.get("sha256") != data_hash:
                    errors.append(f"SHA-256 mismatch for: {name}")
            extra = set(names) - expected - {MANIFEST}
            if extra:
                errors.append("Unmanifested ZIP members: " + ", ".join(sorted(extra)[:10]))
            if not artifacts:
                errors.append("Evidence archive contains no manifest-listed artifacts")
    except (OSError, zipfile.BadZipFile, RuntimeError, KeyError) as exc:
        return [f"Archive cannot be verified: {type(exc).__name__}"]
    return errors


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("archive", type=Path)
    args = p.parse_args()
    problems = verify(args.archive)
    for issue in problems:
        print("FAIL:", issue)
    if problems:
        return 1
    print("PASS: ZIP filenames, manifest coverage and every included SHA-256 verified.")
    print("NOT a physical system, signed human evidence or ARM-build certification.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
