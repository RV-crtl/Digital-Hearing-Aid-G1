#!/usr/bin/env python3
"""Portable QA for user gestures, chirp/tone limits, and sleep-wake policy."""
from pathlib import Path
import subprocess, tempfile, shutil, sys
P=Path(__file__).resolve().parents[1]
cc=shutil.which('gcc') or shutil.which('clang')
if not cc: raise RuntimeError('C compiler required for portable QR checks')
with tempfile.TemporaryDirectory() as d:
    exe=Path(d)/('qr_test.exe' if sys.platform=='win32' else 'qr_test')
    cmd=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(P/'Core/Inc'),str(P/'Tests/test_qr_runtime.c'),str(P/'Core/Src/qr_demo_test.c'),str(P/'Core/Src/qr_sleep_policy.c'),'-o',str(exe)]
    subprocess.run(cmd,check=True,cwd=P)
    subprocess.run([str(exe)],check=True,cwd=P)
app=(P/'Core/Src/demo_app.c').read_text()
for marker in ['qr_test_select(1U)','qr_test_select(4U)','qr_sleep_enter();','QrSleep_Update(&s_qr_sleep','s_audio_sleep_active','AT+BA01','AT+B500','QR_BT_CONNECT_VOLUME_COMMAND','s_tc_candidate_blocks>=96U']:
    if marker not in app:raise AssertionError(f'missing new runtime path: {marker}')
print('PASS: Quiet Riot demo test controls, logical sleep, BT supervision, stable TC channel policy')
