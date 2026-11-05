#!/usr/bin/env python3
"""Offline integration checks for full G1 board source. Not a substitute for an ARM link or HIL."""
from pathlib import Path
import re, sys
root=Path(__file__).resolve().parent.parent
src=(root/'Core/Src/g1_product_board.c').read_text()
app=(root/'Core/Src/demo_app.c').read_text()
main=(root/'Core/Src/main.c').read_text()
h=(root/'Core/Inc/stm32l4xx_hal_conf.h').read_text()
checks={
 'CubeIDE metadata':(root/'.project').is_file() and (root/'.cproject').is_file() and len(list(root.glob('*.ioc')))==1,
 'Microphone DFSDM PC2/PB14 pins':all(x in src for x in ['GPIO_PIN_2','GPIO_PIN_14','GPIO_AF6_DFSDM1','DFSDM1_Channel2']),
 'Microphone 48k 1.92MHz clocking':all(x in src for x in ['Divider=25U','Oversampling=40U','DFSDM_CHANNEL_OUTPUT_CLOCK_SYSTEM']),
 'Dedicated DMA1_CH4':all(x in src for x in ['DMA1_Channel4','DMA_REQUEST_0','DMA1_Channel4_IRQHandler']),
 'Microphone wakeup IRQ': 'Demo_NotifyMicReadyFromISR();' in src and 'portYIELD_FROM_ISR(higher)' in app,
 'Touch I2C1 MPR121 12 pads':all(x in src for x in ['I2C1','GPIO_AF4_I2C1','e<12U','s_touch_integrator[12]','touch_rising','touch_proximity']),
 'Rotary decoder PA0/PA1':all(x in src for x in ['GroupEncoder_Init','GroupEncoder_Update','GPIO_PIN_0|GPIO_PIN_1']),
 'Multiple real audio sources':all(x in app for x in ['process_bt_words(','process_tc_words(','G1_MicTakeBlock(','DEMO_MODE_MIC_ONLY']),
 'Speaker FIFO': 'output_queue_push(g_demo_processed_mono)' in app,
 'Bounded learning':all(x in app for x in ['G1Pref_Observe(','G1Pref_Feedback(','G1Pref_Get(','s_pref_enabled']),
 'Hardware-independent boot': 'G1_BoardInit();' in main and 'product_ui_poll(now);' in app,
 'Peripheral HAL configured': all(x in h for x in ['#define HAL_I2C_MODULE_ENABLED','#define HAL_DFSDM_MODULE_ENABLED']),
 'Live/Recovery parity':all((root/'Core'/n).read_bytes()==(root/'Recovery/Core'/n).read_bytes() for n in ['Src/demo_app.c','Src/g1_product_board.c','Inc/g1_product_board.h','Inc/demo_app.h','Src/main.c','Inc/stm32l4xx_hal_conf.h']),
 'Vendor restore script': 'STM32Cube_FW_L4_V1.18' in (root/'PREPARE_PROJECT.ps1').read_text(),
 'No known FreeRTOS include inversion':app.index('#include "FreeRTOS.h"')<app.index('#include "task.h"')<app.index('#include "queue.h"')
}
for name,okay in checks.items(): print(('PASS' if okay else 'FAIL')+' '+name)
print(f'FULL-SYSTEM SOURCE INTEGRATION: {sum(checks.values())}/{len(checks)} passed (not ARM/hardware tested)')
sys.exit(not all(checks.values()))
