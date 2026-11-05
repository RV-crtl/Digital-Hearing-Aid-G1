#!/usr/bin/env python3
from pathlib import Path
import xml.etree.ElementTree as ET
r=Path(__file__).resolve().parents[1]
n='G1_Product_R26_Adaptive_Audio_CubeIDE119'
a=(r/'Core/Src/demo_app.c').read_text()
t=(r/'Core/Src/telecoil_detector.c').read_text()
assert ET.parse(r/'.project').getroot().findtext('name')==n
assert (r/(n+'.ioc')).is_file()
assert 'ProjectManager.ProjectName='+n in (r/(n+'.ioc')).read_text()
assert 'static volatile uint32_t s_tc_pcm_decode_mode = PCM_DECODE_LOW24;' in a
assert 's_tc_pcm_decode_mode=PCM_DECODE_LOW16;' in a  # preserved runtime fallback
assert 'static volatile uint8_t g_r27_tc_clean_enabled=1U;' in a
assert 'g_r27_tc_clean_enabled = 1U;' in a
assert 'g_demo_control.pcm_decode_mode = PCM_DECODE_LOW24;' in a
assert 'PcmTransport_DecodeStereoWordsMode(words, DEMO_FRAMES_PER_HALF, g_left, g_right,' in a
assert '(PcmTransportDecodeMode_t)s_tc_pcm_decode_mode)' in a
assert 'TC MODE' in a and 'decode=%lu|level24=%lu|cap_q16=%lu|channel=%lu|filter=%lu' in a
assert 'config.present_rms_threshold = 550U;' in t
assert 'config.absent_rms_threshold = 300U;' in t
assert 'config.min_peak_to_peak = 800U;' in t
for rel in ['Core/Src/demo_app.c','Core/Src/telecoil_detector.c','Core/Src/pcm_transport.c','Core/Src/g1_product_board.c','Core/Src/g1_user_controls.c']:
 assert (r/rel).read_bytes()==(r/'Recovery'/rel).read_bytes(),rel
assert (r/'Recovery/.cproject').read_bytes()==(r/'.cproject').read_bytes()
print('PASS R26.6 R23 compatibility: LOW16 fallback preserved; R34 direct LOW24 and historic thresholds available')
