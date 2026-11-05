#!/usr/bin/env python3
"""Capture real G1 UART hardware counters. Does not assume device success."""
import argparse,datetime,json,re,time,pathlib,sys
try: import serial
except ImportError: sys.exit("Install pyserial: py -3 -m pip install pyserial")
ap=argparse.ArgumentParser()
ap.add_argument('--port',required=True,help='e.g. COM7')
ap.add_argument('--seconds',type=int,default=45)
ap.add_argument('--output',default='Validation/results/R12_SERIAL_EVIDENCE.json')
args=ap.parse_args()
rows=[];lines=[];now=time.monotonic()
with serial.Serial(args.port,115200,timeout=.2,write_timeout=1) as conn:
    def send(cmd):conn.write((cmd+'\r\n').encode('ascii'));conn.flush()
    send('WATCH OFF');send('MUTE ON');send('SYSTEM CHECK')
    next_poll=now+1;end=now+max(5,args.seconds)
    while time.monotonic()<end:
        try:
            raw=conn.readline().decode('utf-8','replace').strip()
        except serial.SerialException as exc:
            lines.append('ERROR: '+str(exc));break
        if raw:
            print(raw);lines.append(raw)
            if raw.startswith('G1|CHECK|'):
                fields={}
                for segment in raw.split('|')[2:]:
                    if '=' in segment:
                        k,v=segment.split('=',1)
                        try: fields[k]=int(v,0)
                        except ValueError: fields[k]=v
                rows.append(fields)
        if time.monotonic()>next_poll:
            send('SYSTEM CHECK');next_poll+=1
    send('MUTE ON');send('SOURCE OFF');send('WATCH OFF')
metrics=['mic_blocks','touch_reads','enc_edges','enc_detents','bt_uart_rx','bt_audio','tc_audio','speaker_blocks']
delta={k: (rows[-1].get(k,0)-rows[0].get(k,0)) if len(rows)>1 else None for k in metrics}
result={'captured_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(), 'port':args.port,'snapshots':rows,'deltas':delta,'lines':lines,'note':'Counters are evidence, not acoustic safety or hardware certification'}
out=pathlib.Path(args.output);out.parent.mkdir(parents=True,exist_ok=True)
out.write_text(json.dumps(result,indent=2),encoding='utf-8')
print('Saved',out,'snapshots',len(rows),'deltas',delta)
