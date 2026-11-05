from pathlib import Path
import xml.etree.ElementTree as ET
r=Path(__file__).resolve().parents[1]
u=(r/'Core/Src/g1_user_controls.c').read_text()
h=(r/'Core/Inc/g1_user_controls.h').read_text()
b=(r/'Core/Src/g1_product_board.c').read_text()
a=(r/'Core/Src/demo_app.c').read_text()
assert '*s=(G1UiState){G1_UI_MIC,0U,0U,1U,G1_UI_MAX_GAIN_Q15};' in u
assert 'G1_UI_STEP_Q15 1229U' in h  # R24 still ~5 percentage points with larger safe digital ceiling
assert 's_encoder.transitions_per_step=4U' in b
assert 's_touch_integrator' in b and 's_touch_latched' in b
assert 'product_mic_condition(g_mono)' in a
assert 'G1Pref_Feedback' in a and 'G1Pref_Observe' in a
assert 'g_demo_control.output_gain_q15 = G1_OUTPUT_MAX_Q15;' in a  # R24 bounded startup level
assert 'MICROPHONE' in (r/'Core/Src/demo_app.c').read_text()
assert ET.parse(r/'.project').getroot().findtext('name')=='G1_Product_R26_Adaptive_Audio_CubeIDE119'
for rel in ['Core/Src/g1_user_controls.c','Core/Src/g1_product_board.c','Core/Src/demo_app.c','Core/Inc/g1_user_controls.h']:
 assert (r/rel).read_bytes()==(r/'Recovery'/rel).read_bytes(),rel
print('R21-derived control, R24 startup volume, source, recovery and IDE metadata: PASS')
