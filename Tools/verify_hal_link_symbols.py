#!/usr/bin/env python3
"""R15: HAL I2C from conventional vendor .c; DFSDM from embedded board .inc."""
from pathlib import Path
import re
p=Path(__file__).resolve().parents[1]
board=(p/'Core/Src/g1_product_board.c').read_text()
assert '#include "g1_embedded_hal_dfsdm.inc"' in board
assert '#include "g1_embedded_hal_i2c.inc"' not in board
assert '#include "g1_embedded_hal_i2c_ex.inc"' not in board
assert (p/'Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_dfsdm.c').exists() is False
for sym,n in [('HAL_I2C_IsDeviceReady','stm32l4xx_hal_i2c.c'),('HAL_I2C_Mem_Write','stm32l4xx_hal_i2c.c'),('HAL_I2C_Mem_Read','stm32l4xx_hal_i2c.c'),('HAL_I2CEx_ConfigAnalogFilter','stm32l4xx_hal_i2c_ex.c')]:
 f=p/'Drivers/STM32L4xx_HAL_Driver/Src'/n
 assert f.exists() and re.search(r'\b'+sym+r'\s*\(',f.read_text()),sym
for sym in ('HAL_DFSDM_FilterRegularStart_DMA','HAL_DFSDM_FilterRegularStop_DMA'):
 assert re.search(r'\b'+sym+r'\s*\(',(p/'Core/Inc/g1_embedded_hal_dfsdm.inc').read_text()),sym
print('PASS R15 HAL source symbols/owners (source check; full ARM link not verified)')
