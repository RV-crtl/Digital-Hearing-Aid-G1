#!/usr/bin/env python3
"""Verify production-C filter vectors against independently evaluated biquads."""
from __future__ import annotations
import csv
import math
import sys
from pathlib import Path

HP = (0.9862119246,-1.9724238493,0.9862119246,-1.9722337292,0.9726139693)
EQ = (0.9727371367,-1.7736777085,0.8405655869,-1.7736777085,0.8133027236)

def biquad(xs: list[float], c: tuple[float,...]) -> list[float]:
    b0,b1,b2,a1,a2=c
    x1=x2=y1=y2=0.0
    out=[]
    for x in xs:
        y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2
        x2,x1=x1,x
        y2,y1=y1,y
        out.append(y)
    return out

def response(c: tuple[float,...],hz:float,fs:int=48000)->float:
    w=2*math.pi*hz/fs
    z=complex(math.cos(w),-math.sin(w))
    b0,b1,b2,a1,a2=c
    return abs((b0+b1*z+b2*z*z)/(1+a1*z+a2*z*z))

def main(csv_file:Path)->None:
    data={"hpf":[],"hpf_eq":[]}
    with csv_file.open(newline='',encoding='utf-8') as f:
        reader=csv.DictReader(f)
        assert reader.fieldnames==['configuration','sample','input','output']
        for row in reader:
            key=row['configuration']
            assert key in data
            data[key].append((int(row['sample']),int(row['input']),int(row['output'])))
    for name,rows in data.items():
        assert len(rows)==4096,(name,len(rows))
        assert [r[0] for r in rows]==list(range(4096))
        assert rows[0][1]==10000 and all(r[1]==0 for r in rows[1:])
        expected=biquad([float(r[1]) for r in rows],HP)
        if name=='hpf_eq': expected=biquad(expected,EQ)
        # Production processes floating samples and rounds to int16 between
        # linear filters and final unity-gain output. A two-LSB tolerance
        # accounts for float32 recursion and intermediate PCM quantisation.
        error=max(abs(r[2]-exp) for r,exp in zip(rows,expected))
        print(f'{name}: max |production PCM - theoretical float|={error:.3f} LSB')
        assert error<3.0,(name,error)
    hp_20=20*math.log10(response(HP,20))
    hp_1000=20*math.log10(response(HP,1000))
    eq_1600=20*math.log10(response(EQ,1600))
    print(f'HPF 20 Hz={hp_20:.2f} dB; HPF 1 kHz={hp_1000:.2f} dB; EQ 1.6 kHz={eq_1600:.2f} dB')
    assert hp_20 < -25.0 and hp_1000 > -0.5
    assert -4.0 < eq_1600 < -2.0
    print('PASS: production C vectors, analytic frequency responses, and impulse accuracy')

if __name__=='__main__':
    main(Path(sys.argv[1] if len(sys.argv)>1 else 'Validation/results/audio_vectors.csv'))
