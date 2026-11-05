#!/usr/bin/env python3
"""Compile and execute deterministic portable product-control checks (host-only)."""
from pathlib import Path
import shutil,subprocess,tempfile,sys
r=Path(__file__).resolve().parents[1]
cc=shutil.which('gcc') or shutil.which('clang')
if not cc:
 print('NOT RUN: no host gcc/clang (does not imply product-control pass)')
 sys.exit(0)
with tempfile.TemporaryDirectory() as d:
 cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-I'+str(r/'Core/Inc'),
      str(r/'Tools/test_r21_controls.c'),str(r/'Core/Src/g1_user_controls.c'),
      str(r/'Core/Src/group_encoder.c'),str(r/'Core/Src/adaptive_preferences.c'),
      '-o',str(Path(d)/'test_g1')]
 subprocess.run(cmd,check=True)
 subprocess.run([str(Path(d)/'test_g1')],check=True)
