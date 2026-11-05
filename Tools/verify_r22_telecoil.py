from pathlib import Path
import xml.etree.ElementTree as ET
r=Path(__file__).resolve().parents[1]
p=r/'Core/Src/demo_app.c';s=p.read_text()
assert 'G1|TC|WORDS|' in s and 'G1|TC|ALIGN|' in s and 'G1|TC|BUS|' in s
assert 's_tc_pcm_decode_mode' in s and 's_tc_probe_rms' in s
assert 's_tc_manual_pcm_blocks' in s
assert 'TcCleanup_Block(&g_r27_tc_filter,mono,g_r27_tc_clean,DEMO_FRAMES_PER_HALF)' in s
assert 'process_selected_audio(mono, 1U)' in s
assert (r/'Recovery/Core/Src/demo_app.c').read_bytes()==p.read_bytes()
assert (r/'Recovery/Core/Src/sai.c').read_bytes()==(r/'Core/Src/sai.c').read_bytes()
assert ET.parse(r/'.project').getroot().findtext('name')=='G1_Product_R26_Adaptive_Audio_CubeIDE119'
print('R22 telecoil raw ADC metrics, manual override, independent decode, recovery and CubeIDE identity: PASS')
