#!/usr/bin/env python3
"""R26 source, recovery and deterministic DSP gate. Not CubeIDE or HIL proof."""
import subprocess,sys,shutil,xml.etree.ElementTree as ET
from pathlib import Path
r=Path(__file__).resolve().parents[1]
identity='G1_Product_R26_Adaptive_Audio_CubeIDE119'
assert ET.parse(r/'.project').getroot().findtext('name')==identity
assert (r/(identity+'.ioc')).is_file()
assert identity in (r/'.cproject').read_text()
assert (r/'.cproject').read_bytes()==(r/'Recovery/.cproject').read_bytes()
items=['Core/Src/demo_app.c','Core/Src/tc_cleanup.c','Core/Inc/tc_cleanup.h',
       'Core/Src/g1_scene_ml.c','Core/Inc/g1_scene_ml.h',
       'Core/Src/g1_ambient_dsp.c','Core/Inc/g1_ambient_dsp.h',
       'Core/Src/pcm_transport.c','Core/Inc/pcm_transport.h',
       'Core/Inc/adaptive_preferences.h','Core/Src/tc_hum_cal.c','Core/Inc/tc_hum_cal.h',
       'Core/Src/tc_s24_recovery.c','Core/Inc/tc_s24_recovery.h']
for f in items:
 assert (r/f).read_bytes()==(r/'Recovery'/f).read_bytes(),f+' recovery mismatch'
app=(r/'Core/Src/demo_app.c').read_text()
assert 'G1Ambient_Process(&s_ambient,p,p,DEMO_FRAMES_PER_HALF)' in app
assert 'G1Ambient_Init(&s_ambient)' in app
assert 's_tc_pcm_decode_mode = PCM_DECODE_LOW24;' in app and 'g_r27_tc_clean_enabled = 1U;' in app
assert 'last_control_opportunity_ms' in app and 'vTaskDelay(pdMS_TO_TICKS(1U))' in app
assert 's_tc_notch_auto_enabled' in app
assert 'ML SCENE' in app and 'TC CLEAN' in app
assert '#define G1_DIAGNOSTICS_UART_ENABLE 1' in (r/'Core/Inc/g1_operating_config.h').read_text() # R26.6 bench calibration
assert 'MX_USART1_UART_Init();' in (r/'Core/Src/main.c').read_text()
assert '#if G1_DIAGNOSTICS_UART_ENABLE' in (r/'Core/Src/main.c').read_text()
assert 'xQueueReceive(g_group_events,&ev,0U)' in app
host_cc=shutil.which('gcc') or shutil.which('clang')
for test in ['Tools/test_r26_audio.py','Tools/test_r261_regressions.py','Tools/test_r262_regressions.py','Tools/test_r21_controls.py',
             'Tools/test_r24_tc_filter.py','Tools/test_r263_low24.py','Tools/test_r264_s24_recovery.py','Tools/verify_r263_low24.py','Tools/verify_r24_operating.py',
             'Tools/verify_r23_low16.py','Tools/verify_full_system.py','Tools/test_r266_calibration.py',
             'Tools/verify_r266_release.py','CHECK_PROJECT.py','CHECK_GROUP.py','Tools/r14_build_guard.py',
             'Tools/r15_link_regression.py']:
  if not host_cc and test in ('Tools/test_r26_audio.py','Tools/test_r261_regressions.py','Tools/test_r262_regressions.py','Tools/test_r21_controls.py',
                              'Tools/test_r24_tc_filter.py','Tools/test_r263_low24.py'):
    print('SKIP '+test+': native host C compiler not installed (CubeIDE ARM GCC is separate)')
    continue
  print('CHECK',test,flush=True)
  subprocess.run([sys.executable,str(r/test)],cwd=r,check=True)
print('R26 SOURCE CHECKS PASS; portable C DSP='+('PASS' if host_cc else 'NOT RUN'))
print('NOT a real CubeIDE GCC executable link, board latency, or acoustic validation')
