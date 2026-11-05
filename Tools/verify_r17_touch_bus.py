#!/usr/bin/env python3
from pathlib import Path
p=Path(__file__).resolve().parents[1]
s=(p/'Core/Src/g1_product_board.c').read_text()
d=(p/'Core/Src/demo_app.c').read_text()
h=(p/'Core/Inc/g1_product_board.h').read_text()
for text in ('touch_bb_scan(void)','touch_bb_probe(uint8_t addr)', 'touch_bb_write(uint8_t reg,uint8_t data)', 'touch_bb_read(uint8_t reg,uint8_t *data,uint16_t count)', 'touch_bb_restore_hal()', 'touch_transport=1U', 'HAL_I2C_IsDeviceReady', '0xFCU'):
    assert text in s,text
for text in ('TOUCH BUS','G1|TOUCH|BUS|','G1|UI|ANNA_MAP|E8=BLUETOOTH|E9=TELECOIL|E10=MUTE_TOGGLE|E11=MICROPHONE'):
    assert text in d,text
for field in ('touch_soft_scans','touch_soft_scan_mask','touch_transport','touch_last_probe_status'):
    assert field in h,field
for path in ('Core/Src/g1_product_board.c','Core/Inc/g1_product_board.h','Core/Src/demo_app.c'):
    assert (p/path).read_bytes()==(p/'Recovery'/path).read_bytes(),path
assert (p/(p.name+'.ioc')).is_file()
print('R17 TOUCH BUS checks: PASS')
