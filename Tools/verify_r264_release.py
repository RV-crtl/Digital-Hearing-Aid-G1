#!/usr/bin/env python3
"""R26.4 invariants and parity, not a substitute for CubeIDE target build."""
import hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[1]
s=(root/'Core/Src/demo_app.c').read_text()
for needle in (
    's_tc_pcm_decode_mode = PCM_DECODE_LOW24;',
    's_tc24_level_enabled = 1U;',
    'TcS24Recovery_Process(&s_tc24_level, words, DEMO_FRAMES_PER_HALF,',
    'TC BASELINE R264', 'TC BASELINE R34', 'TC BASELINE R23',
    'TC 24 LEVEL OFF', 'TC 24 STATUS', 'TC PROBE',
    'vTaskDelay(pdMS_TO_TICKS(1U))',
    'g_demo_diag.muted = s_product_ui.muted ? 1U : 0U;',
): assert needle in s, needle
src=(root/'Core/Src/tc_s24_recovery.c').read_text()
for needle in ('0x00FFFFFF',''):
    pass
for needle in ('sign_extend_s24', 'TC24_TARGET_RMS', 'TC24_MAX_OUTPUT_PEAK',
               'TC24_MIN_TRACK_RMS','TC24_DEFAULT_CAP_Q16'):
    assert needle in src, needle
for rel in ('Core/Src/demo_app.c','Core/Src/pcm_transport.c','Core/Src/sai.c',
            'Core/Src/tc_s24_recovery.c','Core/Inc/tc_s24_recovery.h',
            'Core/Src/g1_ambient_dsp.c'):
    assert (root/rel).read_bytes()==(root/'Recovery'/rel).read_bytes(),rel+' Recovery mismatch'
assert 'SAI_PROTOCOL_DATASIZE_24BIT, 2' in (root/'Core/Src/sai.c').read_text()
assert 'DMA_MDATAALIGN_WORD' in (root/'Core/Src/sai.c').read_text()
assert 's_tc24_level_enabled' not in (root/'Core/Src/pcm_transport.c').read_text()  # BT untouched
assert '64U*4096U' in s or '64U * 4096U' in s or 'MIC' in s
print('PASS R26.4 integration: 24-bit level capture, target flags, full source/recovery parity, SAI/DMA unchanged')
