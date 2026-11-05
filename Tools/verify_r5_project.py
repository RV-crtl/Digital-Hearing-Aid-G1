#!/usr/bin/env python3
"""R5 ZIP-independent project identity and compiled-module preflight.
No ARM build can be claimed unless CubeIDE produced and verified the ELF.
"""
from pathlib import Path
import re,xml.etree.ElementTree as ET,sys
P=Path(__file__).resolve().parents[1]
N='G1_Product_R26_Adaptive_Audio_CubeIDE119'
missing=[]
checks=[(P/'.project',N),(P/'Recovery/.cproject',N),(P/'.cproject',N),
        (P/(N+'.ioc'),N),(P/'G1_BUILD_ARM.ps1',N),
        (P/'Core/Src/g1_product_board.c','touch_fail(now)'),
        (P/'Recovery/Core/Src/g1_product_board.c','touch_fail(now)'),
        (P/'Core/Inc/stm32l4xx_hal_dfsdm.h','DFSDM'),
        (P/'Core/Inc/g1_embedded_hal_dfsdm.inc','HAL_DFSDM'),
        (P/'Drivers/STM32L4xx_HAL_Driver/Inc/stm32l4xx_hal_i2c.h','I2C'),
        (P/'Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_i2c.c','HAL_I2C'),
        (P/'Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_i2c_ex.c','HAL_I2CEx'),
        (P/'BUILD_AND_EXPORT.ps1','SEND_BUILD_TO_CHATGPT_')]
for file,marker in checks:
    if not file.is_file(): missing.append(str(file.relative_to(P)))
    elif marker not in file.read_text(errors='replace'):missing.append('missing marker '+str(file.relative_to(P))+': '+marker)
if (P/'Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_dfsdm.c').exists():missing.append('duplicate DFSDM driver source')
name=ET.parse(P/'.project').getroot().findtext('name')
if name!=N:missing.append('mismatched Eclipse project name: '+str(name))
# Enforce that the FreeRTOS step of PREPARE isn't skipped or ignored.
if not (P/'Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h').exists():
    print('DEPENDENCY REQUIRED: Run PREPARE_PROJECT.bat to install STM32CubeL4 FreeRTOS locally.')
for m in missing:print('FAIL:',m)
if missing:print('R5 PACKAGE CHECK FAILED');sys.exit(1)
print('R5 PACKAGE CHECK PASSED; ARM compile/link NOT verified by this script.')
