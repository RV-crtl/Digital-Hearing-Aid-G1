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
    ".project", ".cproject", "261104-Digital_Hearing_Aid.ioc",
    "PREPARE_PROJECT.bat", "Core/Inc/FreeRTOSConfig.h",
    "Core/Src/group_features.c", "Core/Src/group_guardian.c",
    "Core/Src/group_ui.c", "Core/Inc/group_ui.h",
    "Core/Src/rtos_stats_timer.c", "Core/Src/tc_idle_squelch.c",
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

# There is intentionally one active firmware, no Recovery or reference copies.
if (ROOT / "Recovery").exists() or (ROOT / "References").exists():
    ERRORS.append("legacy project trees must not be present")
if len(list(ROOT.rglob("main.c"))) != 1 or len(list(ROOT.rglob("*.ioc"))) != 1:
    ERRORS.append("expected one main.c and one CubeMX .ioc")

if ERRORS:
    for e in ERRORS:
        print("FAIL:", e)
    sys.exit(1)
print("PASS: GitHub documentation navigation, CubeIDE entry points, KiCad files and canonical firmware")
