#!/usr/bin/env python3
from pathlib import Path
import subprocess,shutil,tempfile
root=Path(__file__).resolve().parents[1]
cc=shutil.which('gcc') or shutil.which('clang')
if not cc: raise SystemExit('No host C compiler: test not run')
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp)/'ui'
 cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-I'+str(root/'Core/Inc'),str(root/'Tools/test_r21_controls.c'),str(root/'Core/Src/g1_user_controls.c'),str(root/'Core/Src/group_encoder.c'),str(root/'Core/Src/adaptive_preferences.c'),'-o',str(p)]
 subprocess.run(cmd,check=True);subprocess.run([str(p)],check=True)
print('PASS R24 portable UI control regression')
