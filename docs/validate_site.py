#!/usr/bin/env python3
"""Fast, deterministic source-only integrity checks for G1 GitHub Pages."""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "docs"
REQUIRED = ["index.html", "styles.css", "app.js", "viewer.js", "favicon.svg",
            "render_kicad.py", "validate_site.py", "system-lab.js"]
SECTIONS = ["architecture", "device", "signal", "schematics", "firmware", "verification"]
SCHEMATICS = ["Connectivity", "Audio", "Telecoil_Buffer_Circuits",
              "Analog_Frontend", "Amplifier", "Output", "Power",
              "Processor", "STM32", "User Interface", "Active_Bandpass_Filter",
              "Mid-Supply_Buffer", "CCP"]
CODE = ["Core/Src/demo_app.c", "Core/Src/group_features.c",
        "Core/Src/connectivity_controller.c", "Core/Src/group_ui.c",
        "Core/Src/pcm_transport.c"]


def check() -> list[str]:
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
    if not all(token in viewer for token in ["GLTFLoader", "ThreeMFLoader", "OrbitControls", "g1-assembly.glb", "placeholderAssembly"]):
        errors.append("3D viewer lacks true-CAD loading or fallback")
    if not all(token in html for token in ['id="schematicDialog"', 'id="codePreview"', 'id="modelViewport"']):
        errors.append("Interactive page components missing")
    if not all(name in js for name in SCHEMATICS):
        errors.append("Schematic navigation omits an original KiCad sheet")
    for name in CODE:
        if not (ROOT / name).is_file() or name not in html:
            errors.append(f"Code explorer references missing firmware source: {name}")
    if "/Digital-Hearing-Aid-G1/assets" in html or 'href="/' in html or 'src="/' in html:
        errors.append("Absolute static paths break GitHub project Pages")
    if (SITE / "assets" / "g1-assembly.glb").is_file():
        model = SITE / "assets" / "g1-assembly.glb"
        if model.read_bytes()[:4] != b"glTF":
            errors.append("3D model exists but is not a binary GLB")
    else:
        print("NOTICE: full CAD GLB not yet committed; interactive illustrative fallback is enabled")
    return errors


if __name__ == "__main__":
    problems = check()
    for issue in problems:
        print("FAIL:", issue)
    if problems:
        sys.exit(1)
    print("PASS: website navigation, source paths, SVG-gallery hooks and 3D viewer fallbacks.")
