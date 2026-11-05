#!/usr/bin/env python3
"""Regression for R14's 44 duplicate HAL-I2C linker symbols."""
from pathlib import Path
import re,subprocess,sys
p=Path(__file__).resolve().parents[1]
board=(p/'Core/Src/g1_product_board.c').read_text()
for x in ('g1_embedded_hal_i2c.inc','g1_embedded_hal_i2c_ex.inc'):
 assert x not in board, 'I2C re-embedded in G1 board: '+x
 assert not (p/'Core/Inc'/x).exists(),'Obsolete embedded implementation still packaged: '+x
assert (p/'Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_i2c.c').exists()
assert (p/'Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_i2c_ex.c').exists()
for folder in ('Core/Src','Drivers/STM32L4xx_HAL_Driver/Src'):
 for f in (p/folder).glob('*.c'):
  if f.name != 'g1_product_board.c':
   assert 'g1_embedded_hal_i2c.inc' not in f.read_text(errors='replace'),f.name
assert 'stm32l4xx_hal_i2c.c' not in (p/'Core/Src/g1_product_board.c').read_text().splitlines()[-5:], 'duplicate I2C inclusion'
subprocess.run([sys.executable,str(p/'Tools/r14_build_guard.py')],check=True)
print('R15 duplicate-I2C-linker-errors regression PASS (source ownership)')
