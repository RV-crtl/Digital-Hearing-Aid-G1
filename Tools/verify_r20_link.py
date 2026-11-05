#!/usr/bin/env python3
"""R20 source symbol ownership, linker regression, and standalone control checks."""
from pathlib import Path
import re
r=Path(__file__).resolve().parents[1]
name='G1_Product_R26_Adaptive_Audio_CubeIDE119'
main=(r/'Core/Src/main.c').read_text()
app=(r/'Core/Src/demo_app.c').read_text()
head=(r/'Core/Inc/g1_boot_diag.h').read_text()
assert re.search(r'\bvoid\s+G1_BootReport\(void\)\s*\{',app), 'BOOT STATUS reporter not defined in demo_app.c'
assert re.search(r'\bvoid\s+G1_BootCapture\s*\(',app), 'Reset snapshot not implemented'
assert 'G1_BootReport();++g_demo_diag.console_commands_ok;' in app
assert not re.search(r'\bvoid\s+G1_BootReport\(void\)\s*\{',main), 'Boot reporter has duplicate owner in main.c'
assert 'G1_BootCapture(' in main and 'G1_BootReport();' in main
assert '#include <stdint.h>' in head
for filename in ['main.c','demo_app.c']:
 assert (r/'Core/Src'/filename).read_bytes()==(r/'Recovery/Core/Src'/filename).read_bytes(),filename
assert (r/'Core/Inc/g1_boot_diag.h').read_bytes()==(r/'Recovery/Core/Inc/g1_boot_diag.h').read_bytes()
assert (r/'.cproject').read_bytes()==(r/'Recovery/.cproject').read_bytes()
assert (r/(name+'.ioc')).exists()
assert '<name>'+name+'</name>' in (r/'.project').read_text()
assert 'G1Ui_Touch' in app and 'G1Ui_Encoder' in app
assert 'Demo_SetError(0xC220U' not in app
assert 'G1|TC|STATUS|' in app
print('R20 BOOT REPORT ownership, recovery, controls and project identity: PASS')
