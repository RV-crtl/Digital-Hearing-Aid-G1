#!/usr/bin/env python3
"""G1 STM32CubeIDE 1.19 / CubeMX 6.15 post-generation build guard."""
from pathlib import Path
import re,sys,json,hashlib
root=Path(__file__).resolve().parents[1]
core=root/'Core/Src'; recovery=root/'Recovery/Core/Src'
errors=[];checks=[]
def require(condition,description):
    checks.append(description)
    if not condition: errors.append(description)
def read(path):return path.read_text(encoding='utf-8',errors='replace') if path.exists() else ''
name='261104-Digital_Hearing_Aid'
require(name in read(root/'.project'),'unique CubeIDE project ID')
require(name in read(root/'.cproject'),'consistent build configuration ID')
require((root/(name+'.ioc')).exists(),'STM32CubeMX 6.15 pinout model is present')
require('MxCube.Version=6.15.0' in read(root/(name+'.ioc')),'STM32CubeIDE 1.19 uses matching CubeMX 6.15')
for fn in ('stm32l4xx_it.c','g1_product_board.c','speaker_output.c'):
    require((core/fn).is_file(),f'active {fn}')
    require((recovery/fn).is_file(),f'recovery {fn}')
    require(read(core/fn)==read(recovery/fn),f'active and recovered {fn} match')
owners={
    'DMA1_Channel4_IRQHandler':'g1_product_board.c',
    'DMA1_Channel7_IRQHandler':'speaker_output.c',
    'DMA1_Channel6_IRQHandler':'stm32l4xx_it.c',
    'DMA2_Channel1_IRQHandler':'stm32l4xx_it.c',
}
for irq,owner in owners.items():
    hits=[]
    for f in core.glob('*.c'):
        if re.search(r'\bvoid\s+'+re.escape(irq)+r'\s*\(',read(f)):hits.append(f.name)
    require(hits==[owner],f'{irq} is implemented ONLY by {owner}, got {hits}')
for fn in ('dfsdm.c','i2c.c'):
    require(not (core/fn).exists(),f'no duplicate generated Core/Src/{fn}')
require(not (root/'Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_dfsdm.c').exists(),'DFSDM vendor source does not duplicate board include')
for fn in ('stm32l4xx_hal_i2c.c','stm32l4xx_hal_i2c_ex.c'):
    require((root/'Drivers/STM32L4xx_HAL_Driver/Src'/fn).exists(),f'vendor I2C compiled once from Drivers: {fn}')
    require((root/'Drivers/STM32L4xx_HAL_Driver/Src'/fn).read_bytes()==(root/'Recovery/Drivers/STM32L4xx_HAL_Driver/Src'/fn).read_bytes(),f'vendor I2C recovery matches: {fn}')
board=read(core/'g1_product_board.c')
require('HAL_DMA_IRQHandler(&hdma_mic)' in board,'microphone interrupt uses real local DMA handle')
require('HAL_DFSDM_FilterRegularStart_DMA' in board,'working microphone capture retained')
require('g1_embedded_hal_dfsdm.inc' in board and 'g1_embedded_hal_i2c.inc' not in board,'DFSDM embedded once, I2C NOT embedded')
for spec in ('Core/Inc/g1_embedded_hal_dfsdm.inc',):
    require((root/spec).exists(),f'essential vendor code is packaged: {spec}')
require('<entry flags="VALUE_WORKSPACE_PATH|RESOLVED" kind="sourcePath" name="Drivers"/>' in read(root/'.cproject'),'CubeIDE compiles Drivers/Src files')
startup=read(core/'main.c')
require('G1_BoardInit()' in startup,'known-good board initialization executes')
ioc=read(root/(name+'.ioc'))
require('Dma.RequestsNb=2' in ioc and 'Dma.Request2=' not in ioc,'CubeMX does not re-generate manually owned DFSDM/SAI2B DMA')
for sig in ('I2C1_SCL','I2C1_SDA','USART1_TX','USART1_RX','GPXTI0','GPXTI1','S_CKOUTDFSDM1','S_DATAIN2DFSDM1','SAI2_SD_B'):
    require(sig in ioc,f'IOC contains PCB signal {sig}')
report={'passed':len(checks)-len(errors),'checks':len(checks),'errors':errors}
print(json.dumps(report,indent=2))
sys.exit(1 if errors else 0)
