#!/usr/bin/env python3
"""Clang Cortex-M4 syntax/compile check with temporary minimal libc declarations.
Not equivalent to real CubeIDE ARM GCC/newlib link; never reports hardware PASS."""
from pathlib import Path
import shutil,subprocess,json,sys,datetime,tempfile
R=Path(__file__).resolve().parents[1]
clang=shutil.which('clang')
if not clang:sys.exit('NOT RUN: Clang not installed. Use full CubeIDE build on Windows.')
incs=['Tools/arm_syntax_stubs','Core/Inc','Drivers/STM32L4xx_HAL_Driver/Inc','Drivers/STM32L4xx_HAL_Driver/Inc/Legacy','Drivers/CMSIS/Device/ST/STM32L4xx/Include','Drivers/CMSIS/Include','Middlewares/Third_Party/FreeRTOS/Source/include','Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2','Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F']
args=[clang,'--target=arm-none-eabi','-mcpu=cortex-m4','-mthumb','-mfpu=fpv4-sp-d16','-mfloat-abi=hard','-std=gnu11','-fsyntax-only','-DDEBUG','-DSTM32L476xx','-DUSE_HAL_DRIVER']+['-I'+str(R/s) for s in incs]
paths=['Core/Src/*.c','Drivers/STM32L4xx_HAL_Driver/Src/*.c','Middlewares/Third_Party/FreeRTOS/Source/*.c','Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/*.c','Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/*.c','Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/*.c']
files=sorted({x for glob in paths for x in R.glob(glob)})
fail=[]
for f in files:
 p=subprocess.run(args+[str(f)],capture_output=True,text=True)
 if p.returncode:fail.append({'file':str(f.relative_to(R)), 'stderr':p.stderr[:1500]})
print(f'CORTEX_M4_SYNTAX_FILES={len(files)} PASS={len(files)-len(fail)} FAIL={len(fail)}')
for f in fail:print('FAIL',f['file'],f['stderr'])
out={'timestamp_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'target':'clang arm-none-eabi cortex-m4 hardfp','libc':'minimal syntax-only declaration stubs (NOT newlib link)','total':len(files),'passed':len(files)-len(fail),'errors':fail,'full_link_verified':False,'hardware_verified':False}
(R/'Validation/R12_ARM_SYNTAX.json').write_text(json.dumps(out,indent=2))
sys.exit(bool(fail))
