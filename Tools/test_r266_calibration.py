#!/usr/bin/env python3
"""Portable real-C R26.6 test runner (not PCB or acoustics)."""
from pathlib import Path
import shutil,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
cc=shutil.which('gcc') or shutil.which('clang')
if not cc:raise SystemExit('FAIL: host C compiler required for R26.6 DSP regression')
with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'r266_calibration'
 cmd=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
   '-I'+str(root/'Core/Inc')]+[str(root/f) for f in (
   'Tools/test_r266_calibration.c','Core/Src/tc_s24_recovery.c',
   'Core/Src/tc_cleanup.c','Core/Src/tc_hum_cal.c')]+['-lm','-o',str(exe)]
 subprocess.run(cmd,check=True)
 subprocess.run([str(exe)],check=True)
