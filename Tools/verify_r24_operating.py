#!/usr/bin/env python3
from pathlib import Path
import xml.etree.ElementTree as ET
r=Path(__file__).resolve().parents[1]
n='G1_Product_R26_Adaptive_Audio_CubeIDE119'
a=(r/'Core/Src/demo_app.c').read_text(); m=(r/'Core/Src/main.c').read_text()
c=(r/'Core/Inc/g1_operating_config.h').read_text(); f=(r/'Core/Src/tc_cleanup.c').read_text()
assert ET.parse(r/'.project').getroot().findtext('name')==n
assert (r/(n+'.ioc')).exists() and 'ProjectManager.ProjectName='+n in (r/(n+'.ioc')).read_text()
assert '#define G1_DIAGNOSTICS_UART_ENABLE 1' in c # explicitly bench firmware
assert '#if G1_DIAGNOSTICS_UART_ENABLE\n  MX_USART2_UART_Init();' in m
assert 'MX_USART1_UART_Init();' in m
assert '#if !G1_DIAGNOSTICS_UART_ENABLE' in a and 'static uint8_t s_r10_watch_enabled = 0U;' in a
assert 'g_r27_tc_clean_enabled = 1U;' in a and 's_tc_pcm_decode_mode = PCM_DECODE_LOW24;' in a
assert 'g_demo_control.pcm_decode_mode = PCM_DECODE_LOW24;' in a
assert 'G1Ambient_Process(&s_ambient,p,p,DEMO_FRAMES_PER_HALF);' in a
assert 'G1Scene_Observe' in (r/'Core/Src/g1_ambient_dsp.c').read_text()
assert 'st->gate_gain=1.0f;' in f and 'TcCleanup_SetNotchFrequency' in f and 'const float target=' in f
assert 'last_control_opportunity_ms' in a and 'vTaskDelay(pdMS_TO_TICKS(1U))' in a
assert 'G1_UI_MAX_GAIN_Q15 24576U' in (r/'Core/Inc/g1_user_controls.h').read_text()
for rel in ['Core/Src/demo_app.c','Core/Src/retarget_uart.c','Core/Src/tc_cleanup.c','Core/Src/g1_user_controls.c','Core/Src/main.c','Core/Inc/g1_user_controls.h','Core/Inc/g1_operating_config.h','Core/Src/g1_scene_ml.c','Core/Src/g1_ambient_dsp.c','Core/Inc/g1_scene_ml.h','Core/Inc/g1_ambient_dsp.h']:
 assert (r/rel).read_bytes()==(r/'Recovery'/rel).read_bytes(),rel
assert (r/'.cproject').read_bytes()==(r/'Recovery/.cproject').read_bytes()
print('PASS R26.6: legacy R24 wiring/modes, BT unchanged, LOW24 + continuous TC filter, bench UART, recovery')
