#!/usr/bin/env python3
"""Verify R26.3 LOW24 boot and R34 reference data path, without pretending to test physical hardware."""
from pathlib import Path
import hashlib
r=Path(__file__).resolve().parents[1]
a=(r/'Core/Src/demo_app.c').read_text()
assert 'static volatile uint32_t s_tc_pcm_decode_mode = PCM_DECODE_LOW24;' in a
assert 's_tc_pcm_decode_mode = PCM_DECODE_LOW24;' in a
assert 'g_demo_control.pcm_decode_mode = PCM_DECODE_LOW24;' in a
assert 'g_r27_tc_clean_enabled = 1U;' in a
assert 's_tc_notch_auto_enabled = 0U;' in a
assert 'TC BASELINE R34' in a and 'TC BASELINE R23' in a
assert 'TC PCM LOW16' in a and 'TC PCM LOW24' in a
assert 's_tc_channel_mode = 0U;' in a
# R26.4 retains the unprocessed R34 LOW24 decoder as a runtime reference,
# but the default signed-24-bit recovery branch precedes it.
assert 'PcmTransport_DecodeStereoWordsMode(words, DEMO_FRAMES_PER_HALF, g_left, g_right,' in a
assert '(PcmTransportDecodeMode_t)s_tc_pcm_decode_mode)' in a
assert 'TC BASELINE R34' in a and 's_tc24_level_enabled=0U;' in a
assert 'vTaskDelay(pdMS_TO_TICKS(1U))' in a  # retain control-task starvation fix
for rel in ('Core/Src/demo_app.c','Core/Src/sai.c','Core/Src/pcm_transport.c'):
 assert (r/rel).read_bytes() == (r/'Recovery'/rel).read_bytes(), rel + ' recovery differs'
assert (r/'Core/Src/g1_ambient_dsp.c').read_bytes() == (r/'Recovery/Core/Src/g1_ambient_dsp.c').read_bytes()
assert 'SAI_PROTOCOL_DATASIZE_24BIT, 2' in (r/'Core/Src/sai.c').read_text()
assert 'DMA_MDATAALIGN_WORD' in (r/'Core/Src/sai.c').read_text()
print('PASS R26.6: R34 LOW24 path + R26.4 recovery, fixed filter without automatic notch, R23 LOW16 fallback, recovery parity')
