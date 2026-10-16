"""Repo structure/links/firmware safety checks; no STM32 hardware claims.

Run from any directory. Keep these invariants modest, explicit and stable.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
ERRORS = []

def require(path: str):
    if not (ROOT / path).is_file():
        ERRORS.append(f"missing required tracked file: {path}")

REQUIRED = (
    ".project", ".cproject", "261013-Individual_Demonstration_Connectivity.ioc",
    "PREPARE_PROJECT.bat", "Core/Inc/FreeRTOSConfig.h",
    "Core/Src/group_features.c", "Core/Src/group_guardian.c",
    "Core/Src/group_ui.c", "Core/Inc/group_ui.h",
    "Core/Src/rtos_stats_timer.c", "Recovery/Core/Src/group_features.c",
    "Hardware/Original_KiCad/260510-Digital_hearing_aid.kicad_pro",
    "Hardware/Original_KiCad/260510-Digital_hearing_aid.kicad_pcb",
    "Validation/matlab/verify_audio_filters.m",
    "Validation/c/benchmark_profiles.c", "Documentation/CODE_MAP.md",
    "Documentation/VALIDATION_LADDER.md", "Documentation/REPOSITORY_GUIDE.md",
    "Documentation/G5_STANDALONE_USER_GUIDE.md",
)
for file in REQUIRED:
    require(file)

for filename in ("README.md", "Documentation/REPOSITORY_GUIDE.md",
                 "Documentation/CODE_MAP.md", "Documentation/VALIDATION_LADDER.md"):
    p = ROOT / filename
    if not p.is_file():
        continue
    content = p.read_text(encoding="utf-8")
    for dest in re.findall(r"\]\(([^)]+)\)", content):
        target = dest.split("#", 1)[0].split("?", 1)[0]
        if not target or "://" in target or target.startswith("mailto:"):
            continue
        if not (p.parent / target).exists():
            ERRORS.append(f"{filename}: broken relative link {dest}")

main = ROOT / "Core/Src/demo_app.c"
if main.is_file():
    content = main.read_text(encoding="utf-8")
    for symbol in ("StartAudioTask", "Demo_ControlTask", "group_audio_update",
                   "create_child_tasks", "r29_refresh_debug_snapshot"):
        if not re.search(rf"\b{re.escape(symbol)}\s*\(", content):
            ERRORS.append(f"missing actual firmware entry point: {symbol}")

for path in ("group_features.c", "group_guardian.c", "group_ui.c", "demo_app.c"):
    a = ROOT / "Core/Src" / path
    b = ROOT / "Recovery/Core/Src" / path
    if a.is_file() and b.is_file() and a.read_bytes() != b.read_bytes():
        ERRORS.append(f"recovery mirror mismatch: {path}")

if ERRORS:
    for e in ERRORS:
        print("FAIL:", e)
    sys.exit(1)
print("PASS: GitHub documentation navigation, CubeIDE entry points, KiCad files and recovery mirrors")
