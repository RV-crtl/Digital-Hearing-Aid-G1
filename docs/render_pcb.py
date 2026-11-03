#!/usr/bin/env python3
"""Read-only KiCad PCB rendering for the public G1 site (NOT an ERC/DRC check).

Exports vector views of the original tracked PCB. Outputs are generated under
docs/plots/pcb at build time. Original KiCad sources are never modified.
Requires KiCad 10 CLI; schema of manifest is intentionally simple and stable.
"""
from __future__ import annotations

import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Hardware/Original_KiCad/260510-Digital_hearing_aid.kicad_pcb"
DEST = ROOT / "docs/plots/pcb"
VIEWS = (
    ("front", "F.Cu,F.SilkS,F.Mask,Edge.Cuts", False),
    ("back", "B.Cu,B.SilkS,B.Mask,Edge.Cuts", True),
    ("front-copper", "F.Cu,Edge.Cuts", False),
    ("back-copper", "B.Cu,Edge.Cuts", True),
)

def command(slug: str, layers: str, mirror: bool) -> list[str]:
    cmd = [
        "kicad-cli", "pcb", "export", "svg", str(SOURCE),
        "--mode-single", "--layers", layers, "--fit-page-to-board",
        "--exclude-drawing-sheet", "--output", str(DEST / f"{slug}.svg"),
    ]
    if mirror:
        cmd.append("--mirror")
    return cmd

def main() -> int:
    if not SOURCE.is_file():
        raise SystemExit(f"Original PCB not found: {SOURCE}")
    DEST.mkdir(parents=True, exist_ok=True)
    previews = []
    for slug, layers, mirror in VIEWS:
        result = subprocess.run(command(slug, layers, mirror), capture_output=True,
                                text=True, timeout=150)
        target = DEST / f"{slug}.svg"
        if result.returncode != 0 or not target.is_file():
            raise RuntimeError(
                f"KiCad board SVG plot failed ({slug}):\n"
                f"{result.stdout[-1200:]}\n{result.stderr[-1200:]}"
            )
        # Basic structural sanity. SVGs are referenced as images, not injected
        # into the DOM; do not interpret their content as program instructions.
        lead = target.read_text(encoding="utf-8", errors="replace")[:2000]
        if "<svg" not in lead:
            raise RuntimeError(f"Not an SVG: {target}")
        previews.append({"id": slug, "path": f"plots/pcb/{slug}.svg",
                         "layers": layers, "mirrored": mirror})
        print(f"OK {slug}: {target.stat().st_size} bytes")
    data = {"source": "Hardware/Original_KiCad/260510-Digital_hearing_aid.kicad_pcb",
            "view_only": True, "verified_erc_drc": False, "views": previews}
    (DEST / "manifest.json").write_text(json.dumps(data, indent=2) + "\n")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
