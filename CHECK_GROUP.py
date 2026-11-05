#!/usr/bin/env python3
"""Validate group controls/DSP without historical Nucleo or Recovery expectations."""
from pathlib import Path
import sys
R=Path(__file__).resolve().parent
src=(R/"Core/Src/demo_app.c").read_text(errors="replace")
checks=[]
def ck(ok,desc):
    checks.append(bool(ok))
    print(("PASS " if ok else "FAIL ")+desc)
ck("xQueueCreate(8U, sizeof(GroupEvent))" in src,"bounded group event queue")
ck("xQueueSend(g_group_events" in src and "xQueueReceive(g_group_events" in src,
   "ControlTask/AudioTask event handoff")
ck("GroupFeatures_Process(" in src,"listening profile in DSP")
ck("GroupGuardian_Update(" in src,"overload guardian in audio pipeline")
ck("GroupUi_Poll(" in src,"physical group UI task polling")
ck("GROUP_EVENT_SOURCE" in src,"source control event")
ck("vTaskNotifyGiveFromISR(g_audio_task" in src,"ISR-to-AudioTask notification")
for path in ("Core/Src/group_features.c","Core/Inc/group_features.h",
             "Core/Src/group_guardian.c","Core/Inc/group_guardian.h",
             "Core/Src/group_ui.c","Core/Inc/group_ui.h",
             "Tests/test_group_features.c","Tests/test_group_guardian.c",
             "Tests/test_group_ui.c"):
    ck((R/path).is_file(),"live group implementation/test: "+path)
ck(not (R/"Recovery").exists(),"no duplicate Recovery tree")
ck(len(list(R.rglob("main.c")))==1,"single firmware entry point")
print(f"GROUP SOURCE CHECK: {sum(checks)}/{len(checks)} passed; not a board test.")
raise SystemExit(0 if all(checks) else 1)
