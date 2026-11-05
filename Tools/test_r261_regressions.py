#!/usr/bin/env python3
"""Portable host-C real DSP tests; no board, SPL, SNR, or executable ELF claim."""
import shutil,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
cc=shutil.which('gcc') or shutil.which('clang')
if not cc:raise SystemExit('Host GCC/Clang missing; cannot validate R26.1 DSP')
with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'r261_regression'
 command=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'Core/Inc'),
          str(root/'Tools/test_r261_regressions.c'),str(root/'Core/Src/g1_ambient_dsp.c'),
          str(root/'Core/Src/g1_scene_ml.c'),str(root/'Core/Src/tc_cleanup.c'),'-lm','-o',str(exe)]
 subprocess.run(command,check=True)
 subprocess.run([str(exe)],check=True)
