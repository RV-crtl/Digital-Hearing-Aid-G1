#!/usr/bin/env python3
"""Static checks for R11 repair; not an STM32CubeIDE link or board validation."""
from pathlib import Path
import hashlib,re,sys
R=Path(__file__).resolve().parents[1]
board=(R/'Core/Src/g1_product_board.c').read_text()
app=(R/'Core/Src/demo_app.c').read_text()
h=(R/'Core/Inc/g1_product_board.h').read_text()
checks={
 'unique cubeide project': '<name>G1_Product_R26_Adaptive_Audio_CubeIDE119</name>' in (R/'.project').read_text(),
 'recovery build config parity': (R/'.cproject').read_bytes()==(R/'Recovery/.cproject').read_bytes(),
 'recovery board source parity': board==(R/'Recovery/Core/Src/g1_product_board.c').read_text(),
 'recovery app parity': app==(R/'Recovery/Core/Src/demo_app.c').read_text(),
 'recovery board header parity': h==(R/'Recovery/Core/Inc/g1_product_board.h').read_text(),
 'mic init precedes touch bus': board.index('(void)G1_MicReinit();',board.index('void G1_BoardInit(void)')) < board.index('HAL_I2C_Init(&hi2c1_product)',board.index('void G1_BoardInit(void)')),
 'mic independent retry function': 'HAL_StatusTypeDef G1_MicReinit(void)' in board,
 'mic startup stages': all('mic_init_stage='+str(i)+'U' in board for i in range(1,6)),
 'mic diagnostic command': '"MIC STATUS"' in app and '"MIC INIT"' in app,
 'mic restart rate limited': 's_mic_next_start_ms=now+1000U' in app,
 'bluetooth actual uart recover': '"BT RECOVER"' in app and 'HAL_UART_AbortReceive(&huart1)' in app,
 'bluetooth boot discovery': 'PROFILE_THEN_DISCOVERABILITY' in app and 'AT+BA06' in app and 's_bt_pair_request=1U;' in app,
 'bluetooth status readback': 's_bt_status_code' in app and 'STATUS_QUERY_SENT' in app,
 'raw gpio register telemetry': '"BOARD REG"' in app and 'GPIOB->PUPDR' in app,
 'no phantom device connect': 'not_yet_acknowledged' in app or 'NOT_YET_CONFIRMED' in app,
 'single DFSDM definition': 'HAL_DFSDM_FilterRegularStart_DMA' in (R/'Core/Inc/g1_embedded_hal_dfsdm.inc').read_text() and not (R/'Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_dfsdm.c').exists(),
 'included hardware test guide': (R/'R11_MIC_AND_BLUETOOTH_START_HERE.md').exists(),
}
for k,v in checks.items():print(('PASS' if v else 'FAIL'),k)
print('R11_CHECKS',sum(checks.values()),'/',len(checks))
sys.exit(0 if all(checks.values()) else 1)
