#!/usr/bin/env python3
"""Export the tracked KiCad hierarchy to real, zoomable SVG for GitHub Pages.

Requires kicad-cli, installed only inside the opt-in Pages publish workflow.
This is a *viewing* export. It does not conduct ERC/DRC or resolve external
third-party footprint/symbol libraries.
"""
from __future__ import annotations
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHEETS = ROOT / "Hardware" / "Original_KiCad"
SITE = ROOT / "docs"
PLOTS = SITE / "plots" / "schematics"
TOP = SHEETS / "260510-Digital_hearing_aid.kicad_sch"
KNOWN = [
    "Telecoil_Buffer_Circuits", "Active_Bandpass_Filter",
    "Analog_Frontend", "Mid-Supply_Buffer", "User Interface",
    "Connectivity", "Amplifier", "Processor", "STM32", "Audio",
    "Output", "Power", "CCP",
    "260510-Digital_hearing_aid",
]


def norm(s: str) -> str:
    return re.sub(r"[^a-z0-9]", "", s.lower())


def name_for(path: Path) -> str:
    # KiCad names hierarchy exports like "root-Connectivity-Analog Frontend-Power.svg".
    # Classify by the FINAL sheet name. The old contains() approach incorrectly
    # classified every page as "Connectivity" because it appears in the parent path.
    # Standalone source files may contain hyphens within their OWN names.
    # Match the exact file stem before interpreting '-' as hierarchy delimiter.
    for name in KNOWN:
        if norm(path.stem) == norm(name):
            return name.replace("_", " ")
    leaf = path.stem.split("-")[-1].strip()
    aliases = {"audioinput": "Audio", "stm32l476rg": "STM32",
               "controlconnectivityandpower": "Top-level overview"}
    code = norm(leaf)
    if code in aliases:
        return aliases[code]
    for name in KNOWN:
        if norm(name) == code:
            return name.replace("_", " ")
    return leaf


def normalize_embedded_images(path: Path) -> None:
    # KiCad 10 may insert whitespace and omit base64 padding inside embedded
    # screenshots. Normalise to make SVG render in both browsers and SVG tools.
    original = path.read_text(encoding="utf-8")
    def clean(match: re.Match[str]) -> str:
        payload = re.sub(r"\s+", "", match.group(2))
        return match.group(1) + payload + "=" * (-len(payload) % 4)
    fixed = re.sub(r"(data:image/(?:png|jpeg);base64,)([A-Za-z0-9+/=\s]+)",
                   clean, original)
    if fixed != original:
        path.write_text(fixed, encoding="utf-8")


def main() -> int:
    if not TOP.is_file():
        raise SystemExit("KiCad root schematic missing")
    PLOTS.mkdir(parents=True, exist_ok=True)
    cli = shutil.which("kicad-cli")
    error = None
    if cli:
        cmd = [cli, "sch", "export", "svg", "--output", str(PLOTS), str(TOP)]
        print("Exporting native hierarchical KiCad schematic to SVG.")
        run = subprocess.run(cmd, cwd=SHEETS, capture_output=True, text=True, timeout=300)
        if run.returncode:
            error = (run.stderr or run.stdout)[-1200:]
            print("KiCad SVG export warning:", error, file=sys.stderr)
    else:
        error = "kicad-cli not available; source-link gallery fallback retained"
        print("NOTICE:", error)
    # A few sheets are retained as standalone source files outside the saved root
    # hierarchy. Plot them individually to make the gallery more complete.
    if cli:
        for orphan in ("Processor.kicad_sch", "Mid-Supply_Buffer.kicad_sch",
                       "CCP.kicad_sch"):
            source = SHEETS / orphan
            if source.is_file():
                extra = subprocess.run(
                    [cli, "sch", "export", "svg", "--output", str(PLOTS), str(source)],
                    cwd=SHEETS, capture_output=True, text=True, timeout=120)
                if extra.returncode:
                    print(f"NOTICE: standalone {orphan} SVG export unavailable: " +
                          (extra.stderr or extra.stdout)[-400:], file=sys.stderr)
    files = sorted(PLOTS.glob("*.svg"))
    # Exporting a standalone subsystem that also embeds the main hierarchy can
    # duplicate every nested page. Keep one correctly labelled sheet per name:
    # prefer the exported original top-level hierarchy over its repeated copy.
    distinct = {}
    for path in files:
        name = name_for(path)
        prior = distinct.get(name)
        if prior is None or (path.name.startswith("260510-") and
                             not prior.name.startswith("260510-")):
            distinct[name] = path
    selected = set(distinct.values())
    for path in files:
        if path not in selected:
            path.unlink()
    files = sorted(selected)
    for path in files:
        normalize_embedded_images(path)
    # Save only paths inside the curated site directory. Never publish other repo files.
    manifest = {
        "source": "Hardware/Original_KiCad/260510-Digital_hearing_aid.kicad_sch",
        "schematics": [{"name": name_for(p), "path": p.relative_to(SITE).as_posix()}
                       for p in files if p.is_file()],
        "view_only": True, "erc": "NOT_RUN", "drc": "NOT_RUN",
        "warning": error,
    }
    out = SITE / "plots" / "manifest.json"
    out.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"KiCad exported SVG files: {len(files)}")
    # The website still works with a clearly labelled preview if KiCad export fails.
    return 0


if __name__ == "__main__":
    sys.exit(main())
