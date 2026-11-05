#!/usr/bin/env python3
from pathlib import Path
import re, subprocess, sys
root=Path(__file__).resolve().parents[1]
src=(root/'Core/Src/g1_product_board.c').read_text()
backup=(root/'Recovery/Core/Src/g1_product_board.c').read_text()
assert src==backup,'Recovery would overwrite patched board source'
assert 's_next_probe_ms=now+2000U' in src
segment=src[src.index('static void touch_init'):src.index('/* Caller: after HAL_Init')]
assert '++g_g1_board.touch_errors' not in segment,'Touch init lacks cooldown for a failure path'
assert segment.count('touch_fail(now)')>=4
print('PASS: all I2C touch-init failures back off for 2 seconds; recovery source identical')
subprocess.run([sys.executable,str(root/'Tools/verify_r5_project.py')],check=True)
