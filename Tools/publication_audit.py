#!/usr/bin/env python3
"""Conservative audit of tracked distribution files; NOT a legal/security certification.

This check deliberately separates source hygiene from the *human* public-release
clearance (all code/design/document rights and physical device safety).
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from zipfile import ZipFile, BadZipFile

ROOT = Path(__file__).resolve().parents[1]
BLOCKED_PREFIXES = ("Debug/", "Release/", "RESULTS_R26/", "Validation/results/",
                    "Assessment/generated/", "Middlewares/", ".g1-tools-venv/")
BLOCKED_NAMES = {"RESULTS_LOCATION.txt", ".env", ".env.local", "id_rsa", "id_ed25519"}
REQUIRED = ("README.md", "SECURITY.md", "CONTRIBUTING.md",
            "Documentation/PUBLICATION_GATE.md",
            "Documentation/THIRD_PARTY_PROVENANCE.md",
            "Drivers/STM32L4xx_HAL_Driver/LICENSE.txt",
            "Drivers/CMSIS/LICENSE.txt")
# Specific token/credential signatures only; do not print matching secret values.
SECRET_PATTERNS = (
    ("private_key_header", re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----")),
    ("github_token", re.compile(r"gh[pousr]_[A-Za-z0-9]{35,}")),
    ("github_fine_grained_token", re.compile(r"github_pat_[A-Za-z0-9_]{60,}")),
    ("aws_access_key", re.compile(r"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b")),
    ("slack_token", re.compile(r"xox[baprs]-[A-Za-z0-9-]{20,}")),
)
ALWAYS_MANUAL_REVIEW = (".pdf", ".kicad_pcb", ".kicad_sch", ".kicad_pro")
MAX_SCAN_BYTES = 4_000_000


def scan_text(path: str, content: str) -> list[str]:
    """High-confidence rules, with no matched values emitted in diagnostics."""
    return [f"{path}: {name}" for name, pattern in SECRET_PATTERNS
            if pattern.search(content)]


def tracked(root: Path) -> list[str]:
    result = subprocess.run(["git", "ls-files", "-z"], cwd=root, check=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return sorted(x.decode("utf-8", errors="replace")
                  for x in result.stdout.split(b"\0") if x)


def audit(root: Path) -> dict:
    paths = tracked(root)
    known = set(paths)
    errors = [f"Missing publication audit input: {p}" for p in REQUIRED if p not in known]
    manual = []
    skipped = []
    for rel in paths:
        if rel in BLOCKED_NAMES or any(rel.startswith(p) for p in BLOCKED_PREFIXES):
            errors.append(f"Generated/private path is tracked: {rel}")
        file = root / rel
        if not file.is_file():
            errors.append(f"Tracked path unavailable: {rel}")
            continue
        if file.stat().st_size > MAX_SCAN_BYTES:
            # The original supplied 3MF CAD may exceed the text scanner limit.
            # Validate its container and always require manual rights/content review.
            # Other large tracked files still FAIL rather than silently escaping audit.
            if rel == "docs/assets/g1-assembly.3mf":
                try:
                    with ZipFile(file) as archive:
                        if "3D/3dmodel.model" not in archive.namelist():
                            errors.append(f"Malformed original CAD asset: {rel}")
                        else:
                            manual.append(rel)
                except (BadZipFile, OSError):
                    errors.append(f"Invalid original CAD archive: {rel}")
            else:
                skipped.append(rel)
            continue
        if file.suffix.lower() in ALWAYS_MANUAL_REVIEW or rel.startswith("References/"):
            manual.append(rel)
        data = file.read_bytes()
        if b"\0" in data:
            # No OCR or reliable content/license review of binary materials here.
            if rel not in manual:
                manual.append(rel)
            continue
        content = data.decode("utf-8", errors="replace")
        errors.extend(scan_text(rel, content))
    # A repository-wide licence is intentionally not invented for group work.
    project_license = next((p for p in ("LICENSE", "LICENSE.md", "LICENSE.txt")
                            if p in known), None)
    return {
        "source_hygiene": "PASS" if not errors and not skipped else "FAIL",
        "public_release_ready": False,
        "tracked_files": len(paths),
        "high_confidence_findings": sorted(set(errors)),
        "unscanned_large_files": skipped,
        "manual_rights_and_content_review": sorted(set(manual)),
        "project_license": project_license,
        "human_clearance_required": [
            "Written permission from all contributors and relevant institution to redistribute",
            "A project-wide licence selected by actual rights-holders, not by automation",
            "Manual privacy, IP and licence review of PDFs, group prototypes and KiCad assets",
            "Upstream vendor licence preservation and any separate software dependency notices",
            "Electrical/acoustic/device safety validation before any finished-product claim"
        ]
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = audit(ROOT)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"FAIL: cannot inspect tracked repository: {type(exc).__name__}")
        return 2
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"SOURCE HYGIENE: {report['source_hygiene']} ({report['tracked_files']} files)")
    print("PUBLICATION: HUMAN CLEARANCE STILL REQUIRED; not an approval to make public.")
    print(f"Manual content/rights review: {len(report['manual_rights_and_content_review'])} items")
    for item in report["high_confidence_findings"]:
        print("FAIL:", item)
    for item in report["unscanned_large_files"]:
        print("FAIL: tracked file too large for automated scanning:", item)
    return int(report["source_hygiene"] != "PASS")


if __name__ == "__main__":
    sys.exit(main())
