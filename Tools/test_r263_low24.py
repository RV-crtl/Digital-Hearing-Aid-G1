#!/usr/bin/env python3
"""Build/run host regression of R34-equivalent LOW24 sample interpretation."""
from pathlib import Path
import tempfile,subprocess,shutil
root=Path(__file__).resolve().parents[1]
cc=shutil.which('gcc') or shutil.which('clang')
if not cc: raise SystemExit('FAIL: gcc or clang required for host PCM test')
with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'r263_low24'
 subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
     '-I'+str(root/'Core/Inc'),str(root/'Tools/test_r263_low24.c'),
     str(root/'Core/Src/pcm_transport.c'),str(root/'Core/Src/audio_processing.c'),
     '-lm','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
