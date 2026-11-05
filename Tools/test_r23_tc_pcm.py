#!/usr/bin/env python3
from pathlib import Path
import shutil, subprocess, tempfile
r=Path(__file__).resolve().parents[1];cc=shutil.which('gcc') or shutil.which('clang')
if not cc:raise SystemExit('No host compiler: TEST NOT RUN (does not imply pass)')
with tempfile.TemporaryDirectory() as d:
 cmd=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(r/'Core/Inc'),str(r/'Tools/test_r23_tc_pcm.c'),str(r/'Core/Src/pcm_transport.c'),str(r/'Core/Src/audio_processing.c'),str(r/'Core/Src/telecoil_detector.c'),'-lm','-o',str(Path(d)/'test_tc')]
 subprocess.run(cmd,check=True);subprocess.run([str(Path(d)/'test_tc')],check=True)
