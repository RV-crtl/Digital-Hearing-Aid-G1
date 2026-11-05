#!/usr/bin/env python3
"""Serial logger for a real, connected G1 board. Never fabricates measurements."""
import argparse,datetime,pathlib,time,sys

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--port',required=True)
    p.add_argument('--seconds',type=float,default=60)
    p.add_argument('--baud',type=int,default=115200)
    a=p.parse_args()
    try:import serial
    except ImportError: sys.exit('Install pyserial: py -3 -m pip install pyserial')
    dst=pathlib.Path('Validation/results'); dst.mkdir(parents=True,exist_ok=True)
    name='G1_R10_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S')+'.log'
    with serial.Serial(a.port,a.baud,timeout=0.25) as s, (dst/name).open('w',encoding='utf-8') as f:
        time.sleep(0.2)
        for cmd in ('PING','DIAG ALL','WATCH ON'):
            s.write((cmd+'\r\n').encode('ascii'))
            time.sleep(0.06)
        start=time.monotonic()
        while time.monotonic()-start<a.seconds:
            data=s.readline()
            if data:
                line=data.decode('utf-8',errors='replace').strip()
                line=datetime.datetime.now().isoformat(timespec='milliseconds')+' '+line
                print(line,flush=True);f.write(line+'\n');f.flush()
        s.write(b'WATCH OFF\r\n')
    print('Wrote',dst/name)
if __name__=='__main__':main()
