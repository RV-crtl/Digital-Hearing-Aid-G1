#!/usr/bin/env python3
"""Fast, deterministic source-only integrity checks for G1 GitHub Pages."""
from __future__ import annotations

import re
import sys
import json
import argparse
from zipfile import ZipFile, BadZipFile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "docs"
REQUIRED = ["index.html", "styles.css", "app.js", "viewer.js", "favicon.svg",
            "render_kicad.py", "render_pcb.py", "pcb-explorer.js", "demo-lab.js",
            "data/g1-demo-tests.json", "assets/g1-system-current.svg",
            "validate_site.py", "system-lab.js"]
SECTIONS = ["architecture", "device", "signal", "schematics", "firmware", "demonstration", "verification"]
SCHEMATICS = ["Connectivity", "Audio", "Telecoil_Buffer_Circuits",
              "Analog_Frontend", "Amplifier", "Output", "Power",
              "Processor", "STM32", "User Interface", "Active_Bandpass_Filter",
              "Mid-Supply_Buffer", "CCP"]
CODE = ["Core/Src/demo_app.c", "Core/Src/g1_ambient_dsp.c", "Core/Src/g1_scene_ml.c", "Core/Src/tc_cleanup.c", "Core/Src/group_features.c",
        "Core/Src/connectivity_controller.c", "Core/Src/group_ui.c",
        "Core/Src/pcm_transport.c", "Core/Src/scene_features.c", "Core/Src/adaptive_preferences.c"]


def check(built: bool = False) -> list[str]:
    errors: list[str] = []
    for rel in REQUIRED:
        if not (SITE / rel).is_file():
            errors.append(f"Missing site source: {rel}")
    if errors:
        return errors
    html = (SITE / "index.html").read_text(encoding="utf-8")
    js = (SITE / "app.js").read_text(encoding="utf-8")
    viewer = (SITE / "viewer.js").read_text(encoding="utf-8")
    atlas = (SITE / "system-lab.js").read_text(encoding="utf-8")
    if not all(key in atlas for key in ("atlasFlowSteps", "atlasTasks", "atlasLearnButton")):
        errors.append("G11 full system interactive atlas controls absent")
    for name in ("Core/Inc/adaptive_preferences.h", "Core/Src/adaptive_preferences.c", "Tests/test_adaptive_preferences.c", "Core/Inc/scene_features.h", "Core/Src/scene_features.c", "Tests/test_scene_features.c"):
        if not (ROOT / name).is_file(): errors.append("Missing adaptive learner source: " + name)
    for part in SECTIONS:
        if f'id="{part}"' not in html:
            errors.append(f"Missing accessible site section: {part}")
        if f'href="#{part}"' not in html:
            errors.append(f"Missing site navigation target: {part}")
    for target in re.findall(r'(?:src|href)="(\./[^"]+)"', html):
        if not (SITE / target[2:]).is_file():
            errors.append(f"Unresolved local web asset: {target}")
    if not all(token in viewer for token in ["GLTFLoader", "ThreeMFLoader", "OrbitControls", "g1-assembly.3mf", "cadSearch", "fitCamera"]):
        errors.append("3D viewer lacks real-CAD loading, object search or multi-body fit")
    if not all(token in html for token in ['id="schematicDialog"', 'id="codePreview"', 'id="modelViewport"']):
        errors.append("Interactive page components missing")
    if not all(name in js for name in SCHEMATICS):
        errors.append("Schematic navigation omits an original KiCad sheet")
    for name in CODE:
        if not (ROOT / name).is_file() or name not in html:
            errors.append(f"Code explorer references missing firmware source: {name}")
    for token in ("pcbExplorer", "pcbPreview", "cadParts", "cadShowAll", "BLACK_BOX_TEST_PLAN.md", "G1_CURRENT_SYSTEM_REQUIREMENTS.md"):
        if token not in html: errors.append("Missing G1 UI/traceability feature: " + token)
    if "function placeholderAssembly()" in viewer:
        errors.append("Fictional geometry remains in CAD viewer")
    if "/Digital-Hearing-Aid-G1/assets" in html or 'href="/' in html or 'src="/' in html:
        errors.append("Absolute static paths break GitHub project Pages")
    optimized = SITE / "assets" / "g1-assembly.glb"
    if optimized.is_file() and optimized.open("rb").read(4) != b"glTF":
        errors.append("Generated GLB exists but has invalid glTF magic")
    model = SITE / "assets" / "g1-assembly.3mf"
    if not model.is_file():
        errors.append("Authentic hosted 3MF CAD asset is missing")
    else:
        try:
            with ZipFile(model) as archive:
                if "3D/3dmodel.model" not in archive.namelist():
                    errors.append("Hosted 3MF has no 3D/3dmodel.model payload")
        except (BadZipFile, OSError):
            errors.append("Hosted 3MF is not a valid ZIP-based 3MF file")
    # The interactive demonstration must describe exactly the PDF generator's
    # current source; no stale G11 test IDs or fabricated independent web copy.
    try:
        demo=json.loads((SITE/"data/g1-demo-tests.json").read_text(encoding="utf-8"))
        source=json.loads((ROOT/"Assessment/test_cases.json").read_text(encoding="utf-8"))
        if demo!=source or [x.get("id") for x in demo]!=[f"T{i}" for i in range(1,8)]:
            errors.append("Website demonstration tests differ from Assessment/test_cases.json")
    except (OSError, ValueError, TypeError):
        errors.append("Current demonstration JSON missing or malformed")
    if "demo-lab.js" not in html or "g1DemoCases" not in html or "g1-system-current.svg" not in html:
        errors.append("Real demonstration/architecture UI is not connected")
    if built:
        # These outputs are generated by KiCad/Graphviz/3MF conversion when
        # publishing. Requiring them only in --built mode means source CI can
        # run without shipping several megabytes of duplicative generated SVG.
        required_built = [
            "plots/manifest.json", "plots/pcb/manifest.json",
            "plots/g1-system-2026.svg", "assets/g1-assembly.glb",
        ]
        for rel in required_built:
            if not (SITE / rel).is_file(): errors.append("Built website asset missing: "+rel)
        try:
            schematic=json.loads((SITE/"plots/manifest.json").read_text())
            pcb=json.loads((SITE/"plots/pcb/manifest.json").read_text())
            views=schematic.get("schematics",[])
            layers=pcb.get("views",[])
            if len(views)<12 or len(layers)!=4:
                errors.append("KiCad built views incomplete")
            for item in views+layers:
                target=SITE/item["path"]
                if not target.is_file() or "<svg" not in target.read_text(encoding="utf-8",errors="replace")[:3000]:
                    errors.append("Missing or malformed native KiCad SVG "+item["path"])
            glb=SITE/"assets/g1-assembly.glb"
            if glb.is_file() and glb.open("rb").read(4)!=b"glTF":
                errors.append("Built CAD conversion is not a genuine GLB")
        except (OSError, ValueError, TypeError, KeyError):
            errors.append("Built KiCad manifests unreadable or invalid")
    return errors


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--built",action="store_true",help="Require all KiCad, Graphviz and GLB assets after site build")
    args=parser.parse_args()
    problems = check(built=args.built)
    for issue in problems:
        print("FAIL:", issue)
    if problems:
        sys.exit(1)
    print("PASS: site routes, current PCB architecture, real-CAD inspector, KiCad PCB controls and source links.")
