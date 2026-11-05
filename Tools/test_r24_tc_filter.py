#!/usr/bin/env python3
from pathlib import Path
import subprocess,shutil,tempfile
root=Path(__file__).resolve().parents[1];cc=shutil.which('gcc') or shutil.which('clang')
if not cc:raise SystemExit('No host C compiler: NOT RUN')
with tempfile.TemporaryDirectory() as d:
 output=Path(d)/'tc_filter'
 subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'Core/Inc'),str(root/'Tools/test_r24_tc_filter.c'),str(root/'Core/Src/tc_cleanup.c'),'-lm','-o',str(output)],check=True)
 subprocess.run([str(output)],check=True)
