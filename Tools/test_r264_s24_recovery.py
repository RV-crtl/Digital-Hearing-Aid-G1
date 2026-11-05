#!/usr/bin/env python3
"""Compile and run portable signed-S24 recovery tests with host C compiler."""
import shutil,subprocess,tempfile
from pathlib import Path
p=Path(__file__).resolve().parents[1]
cc=shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
if not cc:
    print('SKIP R26.4 portable C test: no host cc/gcc/clang installed; native CubeIDE build STILL REQUIRED')
    raise SystemExit(0)
with tempfile.TemporaryDirectory() as temp:
    exe=Path(temp)/('test_r264.exe')
    cmd=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(p/'Core/Inc'),
        str(p/'Tools/test_r264_s24_recovery.c'),str(p/'Core/Src/tc_s24_recovery.c'),'-lm','-o',str(exe)]
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe)],check=True)
