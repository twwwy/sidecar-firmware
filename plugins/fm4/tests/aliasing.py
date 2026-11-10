#!/usr/bin/env python3
"""Offline oversampling comparison; spectral divergence, NOT a pure alias meter."""
import array, ctypes as C, json, math
from render import HERE, DEFAULT, render, metrics, spectrum, write_wav, source_identity
outdir=HERE/'build'/'reference';outdir.mkdir(exist_ok=True)
lib=C.CDLL(str(HERE/'build'/'filter.dylib'))
lib.decimate.argtypes=[C.POINTER(C.c_float),C.c_int,C.c_int,C.POINTER(C.c_float)]
def downsample(a,factor):
    source=(C.c_float*len(a)).from_buffer(a);output=(C.c_float*(len(a)//factor))()
    assert lib.decimate(source,len(a),factor,output)==len(output)
    return array.array('f',output)
def distance(a,b):
    ea=spectrum(a);eb=spectrum(b);sa=sum(ea) or 1;sb=sum(eb) or 1
    return .5*sum(abs(x/sa-y/sb) for x,y in zip(ea,eb))
results=[]
for alg in range(8):
    p=DEFAULT.copy();p[0]=alg*16;p[1]=127;p[3]=127;p[4]=127;p[5]=64;p[10]=0;p[11]=0;p[14]=127
    takes={}
    for name,factor,library in [('native',1,None),('8x',8,'ref8.dylib'),('16x',16,'ref16.dylib'),('8x_short_delay',8,'ref8-short-delay.dylib')]:
        a=render(p,103,1.0,rate=48000*factor,libpath=HERE/'build'/library if library else None,block=512)
        if factor>1:a=downsample(a,factor)
        # Compare steady portion; FIR edge transients and attacks excluded.
        takes[name]=a[12000:36000]
        if alg in [0,5,6]:write_wav(outdir/f'alg{alg}_{name}.wav',a)
    row=dict(algorithm=alg,note=103,parameters=p,seed=0x464d3401,
             spectral_total_variation_native_vs_8x=distance(takes['native'],takes['8x']),
             spectral_total_variation_8x_vs_16x=distance(takes['8x'],takes['16x']),
             spectral_total_variation_delay_change=distance(takes['8x'],takes['8x_short_delay']),
             metrics={key:metrics(a) for key,a in takes.items()})
    assert all(v['nonfinite']==0 for v in row['metrics'].values())
    results.append(row);print(alg, {k:round(v,5) for k,v in row.items() if k.startswith('spectral')},flush=True)
(outdir/'comparison.json').write_text(json.dumps(dict(identity=source_identity(),method='8x/16x sinf, same host-time feedback delay, centered Blackman sinc lowpass; TV is normalized spectral distance, not isolated alias energy',results=results),indent=2)+'\n')
