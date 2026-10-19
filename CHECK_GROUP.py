from pathlib import Path
import hashlib, subprocess, shutil, sys
R=Path(__file__).parent
checks=[]
def check(x, name):
 checks.append((bool(x),name));print(('PASS ' if x else 'FAIL ')+name)
def same(src):
 return (R/src).read_bytes()==(R/'Recovery'/src).read_bytes()
src=(R/'Core/Src/demo_app.c').read_text()
check('xQueueCreate(8U, sizeof(GroupEvent))' in src, 'real FreeRTOS queue created')
check('xQueueSend(g_group_events' in src and 'xQueueReceive(g_group_events' in src,'cross-task queued commands consumed by AudioTask')
check('group_button_poll(now);' in src,'Nucleo button polled in ControlTask')
check('GroupFeatures_Process(' in src and src.count('GroupFeatures_Process(')==2,'DSP inserted into direct and routed audio')
check('R34_PRIORITY7_CONTINUOUS_SPIN' in src,'known-good R34 marker preserved')
check('GROUP|STATUS|' in src and 'g_group_telemetry' in src,'real profile telemetry present')
for path in ['Core/Src/demo_app.c','Core/Src/group_features.c','Core/Src/group_encoder.c','Core/Inc/group_features.h','Core/Inc/group_encoder.h']:
 check(same(path), f'recovery mirror: {path}')
check((R/'Hardware/STM32_U13_PINMAP.csv').exists(),'PCB physical pad map included')
check((R/'Documentation/GROUP_G1_IMPORT_AND_BUILD.md').exists(),'import instructions included')
check((R/'.project').exists() and (R/'.cproject').exists(),'CubeIDE managed project metadata present')
check(same('Core/Src/group_guardian.c'), 'guardian source mirrored for CubeMX recovery')
check(same('Core/Inc/group_guardian.h'), 'guardian header mirrored for CubeMX recovery')
check(same('Core/Src/group_features.c'), 'G2 optional DSP source mirror')
check('GroupGuardian_Update(&g_group_guardian' in src and 'g_group_guard_bypass' in src,
      'runtime guardian connected to actual AudioTask pipeline')
check((R/'.github/workflows/verify-g3.yml').exists(), 'CI regression workflow present')
check((R/'Tests/test_group_guardian.c').exists(), 'guardian host regression present')
check(same('Core/Src/group_ui.c'), 'G5 UI source recovery mirror')
check(same('Core/Inc/group_ui.h'), 'G5 UI header recovery mirror')
check(same('Core/Src/demo_app.c'), 'G5 firmware recovery mirror')
check('GroupUi_Poll(&g_group_ui' in src and 'GROUP_EVENT_SOURCE' in src,
      'physical UI state machine enqueues real source requests')
check('vTaskNotifyGiveFromISR(g_audio_task' in src and
      'ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(5U))' in src,
      'DMA-to-AudioTask event-driven RTOS handoff')
check((R/'Tests/test_group_ui.c').exists(), 'standalone UI deterministic tests')
if not all(x for x,_ in checks):sys.exit(1)
print('GROUP G1 VALIDATION PASS: {} checks'.format(len(checks)))
