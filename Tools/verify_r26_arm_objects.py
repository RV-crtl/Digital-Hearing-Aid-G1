#!/usr/bin/env python3
"""Real ARM Cortex-M4F object compilation; relocatable link only, not flashable ELF.

Uses local Clang and minimal libc stubs; CubeIDE GCC/newlib and real hardware
remain the release authority. Returns nonzero for ANY uncompiled app source.
"""
import subprocess,shutil,tempfile
from pathlib import Path
r=Path(__file__).resolve().parents[1]
clang=shutil.which('clang')
linker=shutil.which('ld.lld')
if not clang or not linker:
    raise SystemExit('Clang and ld.lld are required for R26 ARM object validation')
inc=('Tools/arm_syntax_stubs','Core/Inc',
     'Drivers/STM32L4xx_HAL_Driver/Inc','Drivers/STM32L4xx_HAL_Driver/Inc/Legacy',
     'Drivers/CMSIS/Device/ST/STM32L4xx/Include','Drivers/CMSIS/Include',
     'Middlewares/Third_Party/FreeRTOS/Source/include',
     'Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2',
     'Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F')
glob=('Core/Src/*.c','Drivers/STM32L4xx_HAL_Driver/Src/*.c',
      'Middlewares/Third_Party/FreeRTOS/Source/*.c',
      'Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/*.c',
      'Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/*.c',
      'Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/*.c')
files=sorted({p for pattern in glob for p in r.glob(pattern)})
base=[clang,'--target=arm-none-eabi','-mcpu=cortex-m4','-mthumb',
      '-mfpu=fpv4-sp-d16','-mfloat-abi=hard','-O2',
      '-ffunction-sections','-fdata-sections',
      '-DDEBUG','-DSTM32L476xx','-DUSE_HAL_DRIVER',
      '-std=gnu11']+['-I'+str(r/x) for x in inc]
with tempfile.TemporaryDirectory() as temp:
    dest=Path(temp)
    objs=[]
    for i,f in enumerate(files):
        ob=dest/f'{i:03}.o'
        res=subprocess.run(base+['-c',str(f),'-o',str(ob)],capture_output=True,text=True)
        if res.returncode:raise SystemExit('ARM COMPILE FAILED '+str(f.relative_to(r))+'\n'+res.stderr[-2200:])
        objs.append(ob)
    # ARM relocatable link catches duplicate definitions and object ABI mismatch.
    output=dest/'r26_objects.o'
    res=subprocess.run([linker,'-m','armelf','-r','-o',str(output)]+[str(o) for o in objs],
                       capture_output=True,text=True)
    if res.returncode:raise SystemExit('ARM RELOCATABLE LINK FAILED\n'+res.stderr[-2200:])
    print(f'PASS ARM Cortex-M4F Clang compilation {len(files)}/{len(files)} + relocatable object link')
    print('LIMITATION: no native GCC/newlib executable ELF or physical acoustic/latency verification')
