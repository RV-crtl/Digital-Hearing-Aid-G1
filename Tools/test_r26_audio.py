#!/usr/bin/env python3
"""Compile and execute deterministic real-C audio/learning tests. Not an ARM HIL test."""
from pathlib import Path
import subprocess,shutil,tempfile
r=Path(__file__).resolve().parents[1]
cc=shutil.which('gcc') or shutil.which('clang')
if cc is None:raise SystemExit('A host C compiler is required')
with tempfile.TemporaryDirectory() as tmp:
    out=Path(tmp)/'r26_tests'
    src=('Tools/test_r26_audio.c','Core/Src/g1_scene_ml.c',
         'Core/Src/g1_ambient_dsp.c','Core/Src/tc_cleanup.c',
         'Core/Src/adaptive_preferences.c')
    command=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
             '-I'+str(r/'Core/Inc')]+[str(r/s) for s in src]+['-lm','-o',str(out)]
    subprocess.run(command,check=True)
    subprocess.run([str(out)],check=True)
