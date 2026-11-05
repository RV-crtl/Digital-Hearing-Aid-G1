#!/usr/bin/env python3
"""Portable regression compilation; never represents board or STM32 ELF testing."""
from pathlib import Path
import shutil,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
cc=shutil.which('gcc') or shutil.which('clang')
if not cc:raise SystemExit('FAIL: native host C compiler required')
with tempfile.TemporaryDirectory() as t:
    exe=Path(t)/'r262'
    subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
        '-I'+str(root/'Core/Inc'),str(root/'Tools/test_r262_regressions.c'),
        str(root/'Core/Src/tc_cleanup.c'),str(root/'Core/Src/g1_user_controls.c'),
        str(root/'Core/Src/pcm_transport.c'),str(root/'Core/Src/audio_processing.c'),
        '-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
