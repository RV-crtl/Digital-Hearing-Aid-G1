
#!/usr/bin/env python3
"""R16 regression checks: only Anna's 4 electrodes, proximity, stereo PCM."""
from pathlib import Path
import re,subprocess,sys
r=Path(__file__).resolve().parents[1]
board=(r/'Core/Src/g1_product_board.c').read_text()
ui=(r/'Core/Src/g1_user_controls.c').read_text()
app=(r/'Core/Src/demo_app.c').read_text()
checks={
 'Anna touch address 7-bit 0x5A': 's_i2c_address=G1_TOUCH_ADDR_7BIT' in board,
 'Anna I2C PCLK1 selection': 'RCC_I2C1CLKSOURCE_PCLK1' in board,
 'Anna I2C timing': '0x10D19CE4' in board,
 'Anna MPR121 config 0xFC': 'touch_write(0x5EU,0xFCU)' in board,
 'MPR121 config 0xFC readback': 'ecr!=0xFCU' in board,
 'MPR121 13th proximity bit': 'touched>>12' in board,
 'E8 Bluetooth': 'if(e&(1U<<8))flags|=source(s,G1_UI_BT)' in ui,
 'E9 Telecoil': 'if(e&(1U<<9))flags|=source(s,G1_UI_TC)' in ui,
 'E10 mute': 'if(e&(1U<<10))flags|=mute(s,' in ui,
 'E11 microphone': 'if(e&(1U<<11))flags|=source(s,G1_UI_MIC)' in ui,
 'Old E0-E7 disabled': 'if(e&(1U<<1))' not in ui,
 'Only meaningful real-touch events': 'G1|INPUT|via=TOUCH' in app,
 'MPR121 HAL error telemetry': 'G1|TOUCH|addr=0x' in app and 'touch_hal_error' in board,
 'Bluetooth default stereo to mono': 'g_demo_control.bt_mono_mode = DEMO_BT_MONO_MIX;' in app,
 'BT channel L/R RMS': 'BT CHANNELS' in app and 'bt_right_rms' in app,
 'BT UART pairing still present': 'BT PAIR' in app and 'BT STATUS' in app,
 'Microphone DMA code retained': 'HAL_DFSDM_FilterRegularStart_DMA' in board,
 'Microphone untouched by touch I2C recovery': 'HAL_I2C_DeInit(&hi2c1_product)' in board,
 'No doubled HAL I2C definitions': '#include "g1_embedded_hal_i2c.inc"' not in board,
 'Recovery active board matches': (r/'Recovery/Core/Src/g1_product_board.c').read_bytes()==(r/'Core/Src/g1_product_board.c').read_bytes(),
 'Recovery active UI matches': (r/'Recovery/Core/Src/g1_user_controls.c').read_bytes()==(r/'Core/Src/g1_user_controls.c').read_bytes(),
 'Recovery active app matches': (r/'Recovery/Core/Src/demo_app.c').read_bytes()==(r/'Core/Src/demo_app.c').read_bytes(),
 'Recovery build config matches': (r/'Recovery/.cproject').read_bytes()==(r/'.cproject').read_bytes(),
 'CubeIDE 1.19 IOC': 'MxCube.Version=6.15.0' in (r/(r.name+'.ioc')).read_text(),
}
for k,v in checks.items():print(('PASS ' if v else 'FAIL ')+k)
print(f'R16_CHECKS={len(checks)} PASS={sum(checks.values())} FAIL={sum(not v for v in checks.values())}')
sys.exit(not all(checks.values()))
