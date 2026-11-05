#!/usr/bin/env python3
"""Current-release invariants; does not certify native STM32CubeIDE ELF or board audio."""
from pathlib import Path
import xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[1]
assert ET.parse(root/'.project').getroot().findtext('name')=='G1_Product_R26_Adaptive_Audio_CubeIDE119'
assert (root/'G1_Product_R26_Adaptive_Audio_CubeIDE119.ioc').is_file()
app=(root/'Core/Src/demo_app.c').read_text();flt=(root/'Core/Src/tc_cleanup.c').read_text()
cal=(root/'Core/Src/tc_hum_cal.c').read_text()
for text in ('s_tc_pcm_decode_mode = PCM_DECODE_LOW24;',
 's_tc24_level_enabled = 1U;', 'g_r27_tc_clean_enabled = 1U;',
 's_tc_notch_auto_enabled = 0U;', 'TcS24Recovery_Process(',
 'TcHumCal_Feed(&s_tc_hum_cal,g_mono,DEMO_FRAMES_PER_HALF)',
 'TcHumCal_Step(&s_tc_hum_cal)', 'TC HUM LEARN','TC HUM STATUS',
 'TC HUM OFF','TC HUM ON','TC BASELINE R264','TC CHANNEL LEFT',
 'TC CHANNEL RIGHT','TC CHANNEL AUTO','vTaskDelay(pdMS_TO_TICKS(1U))'):
 assert text in app,text
for text in ('TcCleanup_SetNotchFrequency','st->notch_mix+=0.001f',
   'st->gate_gain=1.0f','st->signal_gain=1.0f'):
 assert text in flt,text
assert 's->state=TC_HUM_ANALYSING' in cal
assert 's->peak_fraction_permille<300U' in cal
assert 'SAI_PROTOCOL_DATASIZE_24BIT, 2' in (root/'Core/Src/sai.c').read_text()
assert 'DMA_MDATAALIGN_WORD' in (root/'Core/Src/sai.c').read_text()
assert '#define G1_DIAGNOSTICS_UART_ENABLE 1' in (root/'Core/Inc/g1_operating_config.h').read_text()
for f in ('Core/Src/demo_app.c','Core/Src/tc_cleanup.c','Core/Inc/tc_cleanup.h',
 'Core/Src/tc_hum_cal.c','Core/Inc/tc_hum_cal.h','Core/Src/tc_s24_recovery.c',
 'Core/Inc/tc_s24_recovery.h','Core/Src/sai.c','Core/Src/pcm_transport.c',
 'Core/Inc/g1_operating_config.h','Core/Inc/demo_app.h'):
 assert (root/f).read_bytes()==(root/'Recovery'/f).read_bytes(),f
assert (root/'.cproject').read_bytes()==(root/'Recovery/.cproject').read_bytes()
assert 'G1_UI_MAX_GAIN_Q15 24576U' in (root/'Core/Inc/g1_user_controls.h').read_text()
print('PASS R26.6 project invariants, source/recovery parity, PCM1808 24-bit SAI, bench UART and controls')
