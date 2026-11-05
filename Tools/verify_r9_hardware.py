from pathlib import Path
r=Path(__file__).resolve().parents[1]
b=(r/'Core/Src/g1_product_board.c').read_text();h=(r/'Core/Inc/g1_product_board.h').read_text();d=(r/'Core/Src/demo_app.c').read_text();irq=(r/'Core/Src/stm32l4xx_it.c').read_text()
checks={
 'encoder pins interrupt': 'GPIO_MODE_IT_RISING_FALLING' in b,
 'encoder EXTI0/1': 'EXTI0_IRQHandler' in irq and 'EXTI1_IRQHandler' in irq,
 'decoder owns ISR': 'G1_EncoderEdgeIRQ' in irq and 'GroupEncoder_Update' in b,
 'touch probes address straps': 'G1_TOUCH_ADDR_7BIT' in b and 'HAL_I2C_IsDeviceReady' in b,
 'touch prints raw bitmap': 'g_g1_board.touch_raw=' in b,
 'board raw diagnostics': 'BOARD RAW' in d and 'encoder_irq_edges' in h,
 'audio path diagnostics': 'AUDIO PATH' in d and 'micblocks' in d,
 'bt module replies printed': 'G1|BT|RX|' in d,
 'bt diagnostics available': 'BT STATUS' in d,
 'BT pairing request uses documented command': 'AT+BA06' in d and 'BT PAIR' in d,
 'recovery board source identical': (r/'Recovery/Core/Src/g1_product_board.c').read_bytes()==(r/'Core/Src/g1_product_board.c').read_bytes(),
 'recovery application identical': (r/'Recovery/Core/Src/demo_app.c').read_bytes()==(r/'Core/Src/demo_app.c').read_bytes(),
}
for k,v in checks.items():print(('PASS ' if v else 'FAIL ')+k)
if not all(checks.values()):raise SystemExit(1)
