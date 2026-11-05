#!/usr/bin/env python3
"""R15 HAL compile ownership regression. This is not an ARM build."""
from pathlib import Path
import re
p=Path(__file__).resolve().parents[1]
for r in ('Core/Src/g1_product_board.c','Recovery/Core/Src/g1_product_board.c'):
 s=(p/r).read_text()
 for macro in ('HAL_I2C_MODULE_ENABLED','HAL_DFSDM_MODULE_ENABLED'):
  assert re.search(r'^#define '+macro+r'\s*$',s,re.M), (r,macro)
  assert s.index('#define '+macro)<s.index('#include "g1_product_board.h"')
 assert '#include "g1_embedded_hal_dfsdm.inc"' in s
 assert 'g1_embedded_hal_i2c.inc' not in s and 'g1_embedded_hal_i2c_ex.inc' not in s
 assert '#include "stm32l4xx_hal_i2c.h"' in s
assert (p/'Recovery/Core/Src/g1_product_board.c').read_bytes()==(p/'Core/Src/g1_product_board.c').read_bytes()
for n in ('stm32l4xx_hal_i2c.c','stm32l4xx_hal_i2c_ex.c'):
 assert (p/'Drivers/STM32L4xx_HAL_Driver/Src'/n).is_file(),n
 assert (p/'Recovery/Drivers/STM32L4xx_HAL_Driver/Src'/n).is_file(),n
 assert (p/'Drivers/STM32L4xx_HAL_Driver/Src'/n).read_bytes()==(p/'Recovery/Drivers/STM32L4xx_HAL_Driver/Src'/n).read_bytes()
print('PASS R15: independent HAL I2C objects + single embedded DFSDM + matching recovery')
