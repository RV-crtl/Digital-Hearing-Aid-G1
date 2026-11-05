#!/usr/bin/env python3
"""R19 regression gate for specific R18 build break and standalone behaviour."""
from pathlib import Path
import re
r=Path(__file__).resolve().parents[1]
name='G1_Product_R26_Adaptive_Audio_CubeIDE119'
a=(r/'Core/Src/demo_app.c').read_text()
u=(r/'Core/Src/usart.c').read_text()
m=(r/'Core/Src/main.c').read_text()
assert '#include "sai.h"' in a, 'Missing handle declaration for TC STATUS'
assert 'hdma_sai2_a.ErrorCode' not in a
assert 'hsai_BlockA2.hdmarx->ErrorCode' in a
assert 'Demo_SetError(0xC220U' not in a, 'Console UART is still a fatal boot dependency'
assert 'g_demo_diag.console_rx_irq_armed = 0U;' in a
assert 'if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler()' not in u
assert 'if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler()' in u, 'BT UART1 changed!'
assert 'G1Ui_Init(&s_product_ui)' in a
assert (r/'Core/Src/g1_user_controls.c').read_text().find('G1_UI_MIC,0U,0U,1U,G1_UI_MAX_GAIN_Q15')>=0
assert 'G1Ui_Touch' in a and 'G1Ui_Encoder' in a
assert 'G1_BoardInit()' in m and 'Demo_StartNativeRTOS()' in m
assert 'RCC_PLLSOURCE_HSI' in m and 'RCC_PLLSOURCE_HSE' in m
assert 'G1|TC|STATUS|' in a
assert 'G1|TOUCH|' in a
assert (r/'.project').read_text().find('<name>'+name+'</name>')>=0
assert (r/(name+'.ioc')).is_file()
assert (r/'.cproject').read_bytes()==(r/'Recovery/.cproject').read_bytes()
for f in ('demo_app.c','main.c','usart.c','sai.c','g1_product_board.c','g1_user_controls.c','speaker_output.c'):
    assert (r/'Core/Src'/f).read_bytes()==(r/'Recovery/Core/Src'/f).read_bytes(), f
print('R19: TC handle declaration, optional console, boot autonomous UI, recovery and project identity PASS')
