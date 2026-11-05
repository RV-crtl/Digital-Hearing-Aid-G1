from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed
import subprocess,os,sys
P=Path(__file__).resolve().parents[1]
import tempfile,atexit
_TMP=tempfile.TemporaryDirectory(prefix='dha_arm_');atexit.register(_TMP.cleanup)
O=Path(_TMP.name)
incls=['Tools/arm_syntax_stubs','Core/Inc','Drivers/STM32L4xx_HAL_Driver/Inc','Drivers/STM32L4xx_HAL_Driver/Inc/Legacy','Drivers/CMSIS/Device/ST/STM32L4xx/Include','Drivers/CMSIS/Include','Middlewares/Third_Party/FreeRTOS/Source/include','Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2','Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F']
flags=['--target=arm-none-eabi','-mcpu=cortex-m4','-mthumb','-mfpu=fpv4-sp-d16','-mfloat-abi=hard','-O2','-DDEBUG','-DUSE_HAL_DRIVER','-DSTM32L476xx','-ffunction-sections','-fdata-sections']+['-I'+str(P/x) for x in incls]
sources=sorted(list((P/'Core/Src').glob('*.c'))+list((P/'Drivers/STM32L4xx_HAL_Driver/Src').glob('*.c'))+list((P/'Middlewares/Third_Party/FreeRTOS/Source').glob('*.c'))+list((P/'Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2').glob('*.c'))+list((P/'Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F').glob('*.c'))+list((P/'Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang').glob('heap_4.c'))+list((P/'Core/Startup').glob('*.s')))
# mirror only included drivers from build log (same set as CubeIDE linker)
hal_includes={'stm32l4xx_hal.c','stm32l4xx_hal_cortex.c','stm32l4xx_hal_dma.c','stm32l4xx_hal_dma_ex.c','stm32l4xx_hal_exti.c','stm32l4xx_hal_flash.c','stm32l4xx_hal_flash_ex.c','stm32l4xx_hal_flash_ramfunc.c','stm32l4xx_hal_gpio.c','stm32l4xx_hal_i2c.c','stm32l4xx_hal_i2c_ex.c','stm32l4xx_hal_pwr.c','stm32l4xx_hal_pwr_ex.c','stm32l4xx_hal_rcc.c','stm32l4xx_hal_rcc_ex.c','stm32l4xx_hal_sai.c','stm32l4xx_hal_sai_ex.c','stm32l4xx_hal_tim.c','stm32l4xx_hal_tim_ex.c','stm32l4xx_hal_uart.c','stm32l4xx_hal_uart_ex.c'}
sources=[s for s in sources if '/Drivers/STM32L4xx_HAL_Driver/Src/' not in str(s) or s.name in hal_includes]
def one(s):
  out=O/(s.stem+'.o'); res=subprocess.run(['clang',*flags,'-c',str(s),'-o',str(out)],capture_output=True,text=True);return str(s.relative_to(P)),res.returncode,res.stderr[-1600:],out
with ThreadPoolExecutor(max_workers=8) as ex:r=list(ex.map(one,sources))
fail=[(p,err) for p,rc,err,o in r if rc]
print('compiled',len(r)-len(fail),'/',len(r),'fails',len(fail));
for p,err in fail[:15]:print('FAIL',p,err)
if fail:sys.exit(1)
# LLD linker with --unresolved-symbols=ignore-all for sizing, requires fake GNU-only syntax rewrite
ld=(P/'STM32L476RGTX_FLASH.ld').read_text();ld=ld.replace('_estack = ORIGIN(RAM) + LENGTH(RAM); /* end of "RAM" Ram type memory */','')
ld=ld.replace('/* Sections */','_estack = ORIGIN(RAM) + LENGTH(RAM);\n/* Sections */').replace(' (READONLY)','')
s=O/'clang_lld.ld';s.write_text(ld)
objs=[str(o) for p,rc,err,o in r]
cmd=['ld.lld','-flavor','gnu','-m','armelf','-T',str(s),'--unresolved-symbols=ignore-all','-Map='+str(O/'test.map'),'-o',str(O/'test.elf'),*objs]
res=subprocess.run(cmd,capture_output=True,text=True)
print('LLD return',res.returncode,'last-errors\n',res.stderr[-5000:]);
if res.returncode==0:
  sec=subprocess.run(['readelf','-W','-S',str(O/'test.elf')],capture_output=True,text=True,check=True).stdout
  assert '.capture_ram2' in sec and '10006000' in sec and '10000000' in sec
  print('PASS: full ARM C/ASM cross-link SRAM1 and partitioned SRAM2; unresolved standard-library symbols intentionally ignored')
  os.system('readelf -W -S '+str(O/'test.elf')+' | grep -E "\\.bss|freertos_heap|user_heap|\\.data"')

if res.returncode != 0: sys.exit(res.returncode)
print("NOTE: Cross-target Clang/LLD link is NOT an STM32CubeIDE GCC/Newlib firmware link.\n")
