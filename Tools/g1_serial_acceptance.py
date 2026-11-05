#!/usr/bin/env python3
"""Non-audio UART acceptance: tests real target; never pretends a simulated PASS."""
import argparse,sys,time,json,datetime
try: import serial
except ImportError:
 print('Install pyserial: py -3 -m pip install pyserial',file=sys.stderr);sys.exit(2)
ap=argparse.ArgumentParser()
ap.add_argument('--port',required=True,help='Windows COM port, e.g. COM7')
ap.add_argument('--baud',type=int,default=115200)
ap.add_argument('--timeout',type=float,default=4)
args=ap.parse_args()
commands=[
 ('UI STATUS','G1|UI|STATUS|'),
 ('UI MAP','G1|UI|MAP|'),
 ('MUTE ON','G1|ACK|OK|'),
 ('VOLUME 10','G1|ACK|OK|'),
 ('SOURCE MIC','G1|ACK|OK|'),
 ('SOURCE BT','G1|ACK|OK|'),
 ('SOURCE TC','G1|ACK|OK|'),
 ('SOURCE AUTO','G1|ACK|OK|'),
 ('SOURCE MIC','G1|ACK|OK|'),
 ('VOL +','G1|ACK|OK|'),
 ('VOL -','G1|ACK|OK|'),
 ('UI STATUS','G1|UI|STATUS|'),
 ('BOARD STATUS','G1|BOARD|'),
 ('SOURCE OFF','G1|ACK|OK|'),
 ('MUTE ON','G1|ACK|OK|')
]
results=[]
with serial.Serial(args.port,args.baud,timeout=0.2) as ser:
 time.sleep(1)
 ser.reset_input_buffer()
 for cmd,expected in commands:
  ser.write((cmd+'\r\n').encode('ascii'))
  ser.flush()
  lines=[];found=False;deadline=time.monotonic()+args.timeout
  while time.monotonic()<deadline:
   raw=ser.readline()
   if not raw:continue
   line=raw.decode('ascii','replace').strip()
   if not line:continue
   print(line)
   lines.append(line)
   if line.startswith(expected):found=True;break
  results.append({'command':cmd,'expected':expected,'pass':found,'responses':lines})
  print('CHECK:',cmd, 'PASS' if found else 'FAIL')
from pathlib import Path
out=Path('G1_SERIAL_ACCEPTANCE_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S')+'.json')
out.write_text(json.dumps({'port':args.port,'results':results,'all_pass':all(r['pass'] for r in results)},indent=2))
print('Saved',out)
print('IMPORTANT: UART acceptance tests control-plane only. Touch electrodes, microphone/audio, speaker and batteries must be verified physically.')
sys.exit(0 if all(r['pass'] for r in results) else 1)
