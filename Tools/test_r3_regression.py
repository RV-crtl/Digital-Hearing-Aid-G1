"""R5 import/build packaging regression checks; does not perform ARM build."""
from pathlib import Path
import xml.etree.ElementTree as ET, subprocess, sys
root=Path(__file__).resolve().parents[1]
name='G1_Product_R26_Adaptive_Audio_CubeIDE119'
assert ET.parse(root/'.project').getroot().findtext('name')==name
meta=(root/'.cproject').read_text()
assert name in meta and 'workspace_loc:/'+name in meta
assert name in (root/'Recovery/.cproject').read_text()
assert (root/(name+'.ioc')).is_file()
assert not (root/'261013-Individual_Demonstration_Connectivity.ioc').exists()
assert (root/'Core/Inc/stm32l4xx_hal_dfsdm.h').read_bytes()==(root/'Recovery/Core/Inc/stm32l4xx_hal_dfsdm.h').read_bytes()
assert (root/'Core/Inc/g1_embedded_hal_dfsdm.inc').read_bytes()==(root/'Recovery/Core/Inc/g1_embedded_hal_dfsdm.inc').read_bytes()
assert not (root/'Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_dfsdm.c').exists()
assert not (root/'Recovery/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_dfsdm.c').exists()
assert '#include "stm32l4xx_hal_i2c.h"' in (root/'Core/Src/g1_product_board.c').read_text()
assert 'hmic_channel.Init.OutputClock.Activation=ENABLE' in (root/'Core/Src/g1_product_board.c').read_text()
assert 'foreach ($driver in' in (root/'PREPARE_PROJECT.ps1').read_text()
assert 'Tools\\verify_active_project.py' in (root/'PREPARE_PROJECT.ps1').read_text()
for f in ['Core/Src/g1_product_board.c']:
    cmd=['gcc','-std=gnu11','-fsyntax-only','-w','-DSTM32L476xx','-DUSE_HAL_DRIVER']
    cmd += ['-I'+str(root/i) for i in ['Core/Inc','Drivers/STM32L4xx_HAL_Driver/Inc','Drivers/STM32L4xx_HAL_Driver/Inc/Legacy','Drivers/CMSIS/Device/ST/STM32L4xx/Include','Drivers/CMSIS/Include']]
    cmd += [str(root/f)]
    subprocess.run(cmd,check=True)
print('PASS R7 unique CubeIDE identity, single DFSDM implementation, safe recovery, local header, host C syntax')
