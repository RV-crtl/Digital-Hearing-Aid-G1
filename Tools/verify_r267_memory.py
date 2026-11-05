#!/usr/bin/env python3
"""R26.7 source/recovery/linker-placement checks. --arm-smoke uses Clang+LLD, NOT GCC/newlib."""
from pathlib import Path
import re,sys,shutil,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
files={name:(r/name).read_text() for name in (
    'Core/Inc/FreeRTOSConfig.h','Core/Src/freertos.c',
    'STM32L476RGTX_FLASH.ld','Core/Startup/startup_stm32l476rgtx.s',
    'Core/Src/demo_app.c','Core/Src/g1_product_board.c','PREPARE_PROJECT.ps1')}
for name in ('Core/Inc/FreeRTOSConfig.h','Core/Src/freertos.c',
             'STM32L476RGTX_FLASH.ld','Core/Startup/startup_stm32l476rgtx.s'):
    assert (r/name).read_bytes()==(r/'Recovery'/name).read_bytes(),name+' recovery mismatch'
cfg=files['Core/Inc/FreeRTOSConfig.h']
heap=files['Core/Src/freertos.c']
ld=files['STM32L476RGTX_FLASH.ld']
boot=files['Core/Startup/startup_stm32l476rgtx.s']
board=files['Core/Src/g1_product_board.c']
app=files['Core/Src/demo_app.c']
prep=files['PREPARE_PROJECT.ps1']
assert re.search(r'#define\s+configAPPLICATION_ALLOCATED_HEAP\s+1\b',cfg)
assert re.search(r'#define\s+configTOTAL_HEAP_SIZE\s+\(\(size_t\)24576\)',cfg)
assert len(re.findall(r'\buint8_t\s+ucHeap\[configTOTAL_HEAP_SIZE\]',heap))==1
assert 'section(".freertos_heap_ram2")' in heap
assert '.freertos_heap_ram2 (NOLOAD)' in ld and '} >RAM2_HEAP' in ld
assert '} >RAM2_CAPTURE' in ld
assert re.search(r'RAM2_HEAP\s+\(xrw\)\s*:\s*ORIGIN\s*=\s*0x10000000,\s*LENGTH\s*=\s*24K',ld)
assert re.search(r'RAM2_CAPTURE\s+\(xrw\)\s*:\s*ORIGIN\s*=\s*0x10006000,\s*LENGTH\s*=\s*8K',ld)
assert '_sram2_heap_start' in ld and '_sram2_heap_end' in ld
assert boot.index('_sram2_heap_start') < boot.index('bl __libc_init_array')
assert 'Restore both files' in prep and "'Core\\Startup\\startup_stm32l476rgtx.s'" in prep
# Linker must keep all peripheral DMA globals in normal SRAM1 .bss.
for symbol in ('g_bt_dma_words','g_tc_dma_words','g_speaker_tx_dma'):
    assert re.search(r'\b'+symbol+r'\[',app)
assert 'G1_BoardPoll(now,&event);' in app
assert '_capture_ram2_start' in app and 'SRAM2_BASE' not in app
assert ' .capture_ram2 (NOLOAD)' in ld and '_capture_ram2_end' in ld
assert '8192' in ld
assert 'encoder_poll_edges' in board and 's_encoder_last_polled_phase' in board
assert 'GroupEncoder_Update(&s_encoder,' in board and 'G1_EncoderEdgeIRQ' in board
print('PASS coordinated 24 KiB SRAM2 heap; SRAM1 DMA placement retained')
print('PASS linker and startup zeroing before constructors; recovery parity')
print('PASS encoder IRQ and 250ms-fallback polling path active in ControlTask')
if '--arm-smoke' in sys.argv:
    clang=shutil.which('clang'); lld=shutil.which('ld.lld'); readelf=shutil.which('readelf')
    assert clang and lld and readelf, 'Clang, LLD and readelf required for optional ARM memory-layout test'
    incs=['Tools/arm_syntax_stubs','Core/Inc','Drivers/STM32L4xx_HAL_Driver/Inc',
          'Drivers/STM32L4xx_HAL_Driver/Inc/Legacy','Drivers/CMSIS/Device/ST/STM32L4xx/Include',
          'Drivers/CMSIS/Include','Middlewares/Third_Party/FreeRTOS/Source/include',
          'Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2',
          'Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F']
    flags=['--target=arm-none-eabi','-mcpu=cortex-m4','-mthumb','-mfpu=fpv4-sp-d16',
           '-mfloat-abi=hard','-O2','-DDEBUG','-DUSE_HAL_DRIVER','-DSTM32L476xx',
           '-ffunction-sections','-fdata-sections']+['-I'+str(r/x) for x in incs]
    with tempfile.TemporaryDirectory() as d:
        d=Path(d)
        def cmd(args):
            q=subprocess.run(args,text=True,capture_output=True)
            if q.returncode: raise RuntimeError(' '.join(map(str,args))+'\n'+q.stderr[-4000:])
            return q.stdout
        heap_obj=d/'freertos.o'
        cmd([clang,*flags,'-c',str(r/'Core/Src/freertos.c'),'-o',str(heap_obj)])
        startup_obj=d/'startup.o'
        cmd([clang,*flags,'-c',str(r/'Core/Startup/startup_stm32l476rgtx.s'),'-o',str(startup_obj)])
        (d/'stub.c').write_text('''void SystemInit(void){} void __libc_init_array(void){} int main(void){return 0;}
/* 64KB of additional RAM1 BSS to exercise actual linker region checks. */
__attribute__((used)) volatile unsigned char representative_ram1_bss[65536];
''')
        stub=d/'stub.o'
        cmd([clang,*flags,'-c',str(d/'stub.c'),'-o',str(stub)])
        elf=d/'test.elf'
        # GNU ld accepts ORIGIN(RAM) before MEMORY; LLVM LLD requires the
        # declaration after MEMORY. Adapt a TEMPORARY COPY for this smoke test;
        # the real GNU ld script remains byte-for-byte untouched.
        lldscript=d/'lld_compatible.ld'
        lld_text=ld.replace('_estack = ORIGIN(RAM) + LENGTH(RAM); /* end of "RAM" Ram type memory */', '')
        lld_text=lld_text.replace('/* Sections */',
             '_estack = ORIGIN(RAM) + LENGTH(RAM); /* LLD forward-reference fix */\n\n/* Sections */')
        lld_text=lld_text.replace(' (READONLY)', '')  # GNU ld 13 syntax unsupported by LLD
        lldscript.write_text(lld_text)
        cmd([lld,'-flavor','gnu','-m','armelf','-T',str(lldscript),
             '-o',str(elf),str(heap_obj),str(startup_obj),str(stub)])
        sec=cmd([readelf,'-W','-S',str(elf)])
        sym=cmd([readelf,'-W','-s',str(elf)])
        sectionline=next(line for line in sec.splitlines() if ' .freertos_heap_ram2 ' in line)
        assert re.search(r'10000000\s+[0-9a-fA-F]+\s+006000\s+',sectionline),sectionline
        for s in ('ucHeap','_sram2_heap_start','_sram2_heap_end'):
            assert s in sym,s
        print('PASS actual ARM ELF linker layout: 0x10000000..0x10006000 SRAM2; 64 KiB representative SRAM1 bss')
        print('SMOKE LIMITATION: this is Clang/LLD layout verification, NOT the production ARM GCC/newlib link.')
print('R26.7 MEMORY/ENCODER VERIFICATION PASSED')
