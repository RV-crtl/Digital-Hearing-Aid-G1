#!/usr/bin/env python3
"""Host-side syntax regression for Cortex-M application files.

Executes against the actual STM32 HAL headers. It is not a substitute for
ARM/GNU linker validation or tests on the custom PCB.
"""
from __future__ import annotations
import shutil, subprocess, sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
gcc=shutil.which('gcc') or shutil.which('clang')
if gcc is None:
    print('SKIP: native GCC/Clang unavailable; perform CubeIDE ARM build on Windows')
    sys.exit(0)
args=[gcc,'-std=gnu11','-DSTM32L476xx','-DUSE_HAL_DRIVER','-DDEBUG',
      '-fsyntax-only','-w','-Werror=implicit-function-declaration','-Werror=return-type']
# Host-only newlib stand-ins; actual Cortex-M4 compilation uses ARM/newlib.
for d in ('Tools/arm_syntax_stubs','Core/Inc','Drivers/STM32L4xx_HAL_Driver/Inc','Drivers/STM32L4xx_HAL_Driver/Inc/Legacy','Drivers/CMSIS/Device/ST/STM32L4xx/Include','Drivers/CMSIS/Include','Middlewares/Third_Party/FreeRTOS/Source/include','Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2','Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F'):
    if (root/d).is_dir(): args.append('-I'+str(root/d))
source_dirs=['Core/Src','Drivers/STM32L4xx_HAL_Driver/Src']
errors=[];checked=0;skipped=[]
for directory in source_dirs:
    for source in sorted((root/directory).glob('*.c')):
        # Compiling the entire HAL tree can include modules disabled in this
        # project. The managed project only builds the enabled modules.
        if directory.startswith('Drivers') and source.stem not in {
            'stm32l4xx_hal','stm32l4xx_hal_cortex','stm32l4xx_hal_dma',
            'stm32l4xx_hal_dma_ex','stm32l4xx_hal_exti','stm32l4xx_hal_flash',
            'stm32l4xx_hal_flash_ex','stm32l4xx_hal_flash_ramfunc',
            'stm32l4xx_hal_gpio','stm32l4xx_hal_i2c', 'stm32l4xx_hal_i2c_ex',
            'stm32l4xx_hal_pwr','stm32l4xx_hal_pwr_ex','stm32l4xx_hal_rcc',
            'stm32l4xx_hal_rcc_ex','stm32l4xx_hal_sai','stm32l4xx_hal_sai_ex',
            'stm32l4xx_hal_tim','stm32l4xx_hal_tim_ex','stm32l4xx_hal_uart',
            'stm32l4xx_hal_uart_ex'}:
            continue
        if not (root/'Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h').is_file() and source.name in ('demo_app.c','rtos_bootstrap.c','rtos_tick_proof.c'):
            skipped.append((str(source.relative_to(root)),'requires locally installed FreeRTOS')); continue
        # HAL vendor files use ARM exclusive-access intrinsics absent on x86.
        # Avoid treating ARM-only intrinsics as application compile failures.
        extra = ['-Wno-error=implicit-function-declaration'] if directory.startswith('Drivers') else []
        cmd=args+extra+[str(source)]
        result=subprocess.run(cmd,text=True,capture_output=True,cwd=root)
        checked+=1
        if result.returncode: errors.append((str(source.relative_to(root)),(result.stdout+result.stderr)[-2200:]))
for filename,report in errors: print('FAIL:',filename,'\n',report)
for filename,why in skipped: print('SKIP:',filename,'('+why+')')
print(f'HOST C SYNTAX: {checked-len(errors)}/{checked} available files PASS, {len(skipped)} environment skips (ARM/link not tested)')
sys.exit(1 if errors else 0)
