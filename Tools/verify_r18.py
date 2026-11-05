#!/usr/bin/env python3
from pathlib import Path
import sys,hashlib,re
p=Path(__file__).resolve().parents[1]
a=(p/'Core/Src/demo_app.c').read_text()
b=(p/'Core/Src/main.c').read_text()
for f in ('main.c','demo_app.c'):
 assert (p/'Core/Src'/f).read_bytes()==(p/'Recovery/Core/Src'/f).read_bytes(),f+" recovery differs"
assert (p/'Core/Inc/g1_boot_diag.h').read_bytes()==(p/'Recovery/Core/Inc/g1_boot_diag.h').read_bytes()
assert (p/'Recovery/.cproject').read_bytes()==(p/'.cproject').read_bytes()
assert (p/'.project').read_text().find('<name>G1_Product_R26_Adaptive_Audio_CubeIDE119</name>')>=0
assert (p/'G1_Product_R26_Adaptive_Audio_CubeIDE119.ioc').is_file()
assert 'g_dma_topology_mask != dma_desired_topology()' in a
assert 'static uint32_t dma_desired_topology' in a
assert 'const bool need_tc=(topology&2U)!=0U;' in a
assert 's_tc_last_left_rms=lrms' in a
assert 'G1|TC|STATUS|' in a and 'G1|TC|RESTART|' in a
assert 'G1_BootReport();' in b and 'G1_BootReport();' in a and 'RCC_CSR_BORRSTF' in a and 'RCC_CSR_LPWRRSTF' in a
assert 'G1_BootCapture(' in b and 'void G1_BootReport(void)' in a
assert 'RCC_PLLSOURCE_HSI' in b and 'RCC_PLLSOURCE_HSE' in b
assert 'HAL_RCCEx_PeriphCLKConfig' in b
assert 'g_g1_board.mic_running' in a and 'G1_MicStart()' in a
assert (p/'Core/Src/g1_product_board.c').read_bytes()==(p/'Recovery/Core/Src/g1_product_board.c').read_bytes()
assert 'SAI_SYNCHRONOUS_EXT_SAI1' in (p/'Core/Src/sai.c').read_text()
assert '0x5A' in (p/'Core/Src/g1_product_board.c').read_text()
print('R18 regression guard: PASS (source/recovery, TC topology, HSE/HSI fallback, working MIC and Anna touch unchanged)')
