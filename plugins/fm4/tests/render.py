#!/usr/bin/env python3
"""Reproducible native fixtures and spectral metrics; standard library only. MIT."""
import array, cmath, ctypes as C, hashlib, json, math, pathlib, platform, subprocess, wave
HERE=pathlib.Path(__file__).resolve().parents[1]
OUT=HERE/'build'/'fixtures'
FS=48000
SEED=0x464d3401
INIT=C.CFUNCTYPE(C.c_int32,C.c_void_p)
VOID=C.CFUNCTYPE(None,C.c_void_p)
NOTE=C.CFUNCTYPE(None,C.c_void_p,C.c_float,C.c_float)
PARAM=C.CFUNCTYPE(None,C.c_void_p,C.POINTER(C.c_float))
RENDER=C.CFUNCTYPE(None,C.c_void_p,C.POINTER(C.c_float),C.c_int32)
class Host(C.Structure):
    _fields_=[('api',C.c_uint32),('rate',C.c_float),('block',C.c_int32),('log',C.c_void_p),('random',C.c_void_p),('reserved',C.c_void_p*11)]
class Plugin(C.Structure):
    _fields_=[('magic',C.c_uint32),('api',C.c_uint32),('state',C.c_uint32),('cpu',C.c_uint32),('init',INIT),('voice_init',VOID),('on',NOTE),('off',VOID),('params',PARAM),('render',RENDER),('reserved',C.c_void_p*8)]
DEFAULT=[k['default'] for k in json.loads((HERE/'manifest.json').read_text())['knobs']]
def source_identity():
    files=sorted(p for p in HERE.rglob('*') if p.is_file() and 'build' not in p.relative_to(HERE).parts and p.suffix in ('.c','.cpp','.h','.py','.json','.txt') or p.is_file() and p.name=='Makefile')
    hashes={str(p.relative_to(HERE)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    return dict(base_revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=HERE,text=True).strip(),
                source_sha256=hashlib.sha256(json.dumps(hashes,sort_keys=True).encode()).hexdigest(),files=hashes)
def fft(x):
    a=[complex(v) for v in x]; n=len(a); j=0
    for i in range(1,n):
        bit=n>>1
        while j&bit: j^=bit;bit>>=1
        j^=bit
        if i<j:a[i],a[j]=a[j],a[i]
    size=2
    while size<=n:
        root=cmath.exp(-2j*math.pi/size)
        for start in range(0,n,size):
            w=1
            for k in range(start,start+size//2):
                u=a[k];v=a[k+size//2]*w;a[k]=u+v;a[k+size//2]=u-v;w*=root
        size*=2
    return a[:n//2+1]
def spectrum(data,rate=FS):
    n=8192
    # Average three windows spread through the signal, including attack-rich content.
    energy=[0.0]*(n//2+1)
    for start in (0,max(0,len(data)//3-n//2),max(0,2*len(data)//3-n//2)):
        chunk=data[start:start+n]
        window=[(chunk[i] if i<len(chunk) else 0)*(.5-.5*math.cos(2*math.pi*i/(n-1))) for i in range(n)]
        for k,v in enumerate(fft(window)):energy[k]+=abs(v)**2
    return energy

def metrics(data,rate=FS):
    finite=[float(x) for x in data if math.isfinite(x)]
    e=spectrum(data,rate);total=sum(e) or 1
    return dict(peak=max(map(abs,finite),default=0),rms=math.sqrt(sum(x*x for x in finite)/max(1,len(finite))),
                dc=sum(finite)/max(1,len(finite)),nonfinite=len(data)-len(finite),
                centroid_hz=sum(k*rate/8192*v for k,v in enumerate(e))/total,
                above_18khz_energy_fraction=sum(v for k,v in enumerate(e) if k*rate/8192>=18000)/total)

def render(params,note,seconds,off=None,events=(),seed=SEED,rate=FS,libpath=None,block=12,diagnostics=None):
    lib=C.CDLL(str(libpath or HERE/'libfm4.dylib'));pl=Plugin.in_dll(lib,'rmp_entry')
    host=Host(2,rate,block,None,None);assert pl.init(C.byref(host))==0
    state=C.create_string_buffer(pl.state); assert C.addressof(state)%8==0
    lib.fm4_init.argtypes=[C.c_void_p,C.c_float,C.c_uint32]
    assert lib.fm4_init(state,rate,seed)==0
    p=(C.c_float*16)(*params);pl.params(state,p);pl.on(state,note,.85)
    schedule=[(round(t*rate),'params',changes) for t,changes in events]
    if off is not None:schedule.append((round(off*rate),'off',None))
    schedule.sort(key=lambda e:e[0]);schedule.append((round(seconds*rate),'end',None))
    out=array.array('f');buf=(C.c_float*block)();pos=0
    for at,kind,changes in schedule:
        while pos<at:
            n=min(block,at-pos);pl.render(state,buf,n);out.extend(buf[:n]);pos+=n
        if kind=='off':pl.off(state)
        if kind=='params':
            for key,value in changes.items():p[int(key)-1]=value
            pl.params(state,p)
    if diagnostics is not None:
        peaks=(C.c_float*2)();counts=(C.c_uint32*2)()
        lib.fm4_stats.argtypes=[C.c_void_p,C.POINTER(C.c_float),C.POINTER(C.c_uint32)]
        lib.fm4_stats(state,peaks,counts)
        diagnostics.update(pre_safety_peak=peaks[0],post_safety_peak=peaks[1],clamp_samples=counts[0],internal_nonfinite=counts[1])
    return out

def write_wav(path,data,rate=FS):
    pcm=array.array('h',(round(max(-1,min(1,x))*32767) for x in data))
    if __import__('sys').byteorder!='little':pcm.byteswap()
    with wave.open(str(path),'wb') as w:w.setnchannels(1);w.setsampwidth(2);w.setframerate(rate);w.writeframes(pcm.tobytes())

def fixtures():
    def patch(**changes):
        p=DEFAULT.copy()
        for k,v in changes.items():p[int(k[1:])-1]=v
        return p
    return [
      dict(name='default_metallic_pluck',parameters=DEFAULT,note=60,seconds=3),
      dict(name='inharmonic_bell',parameters=patch(p1=16,p2=108,p4=65,p5=18,p7=100,p10=100,p11=36),note=72,seconds=7),
      dict(name='aggressive_bass',parameters=patch(p1=96,p2=54,p4=93,p5=100,p6=30,p11=40,p15=127),note=28,seconds=4,off=2.5),
      dict(name='metallic_percussion',parameters=patch(p1=48,p2=127,p4=87,p5=72,p7=25,p10=22,p11=25),note=47,seconds=2),
      dict(name='evolving_held_texture',parameters=patch(p1=80,p2=90,p4=70,p5=88,p6=0,p7=127,p9=70,p10=95,p11=50,p12=110,p15=127),note=45,seconds=12,off=8),
      dict(name='morph_sweep',parameters=patch(p1=16,p15=127),note=57,seconds=6,off=5,events=[(i*.05,{'8':127*i/100}) for i in range(101)]),
      dict(name='ratio_sweep',parameters=patch(p1=32,p2=0,p15=127),note=48,seconds=7,off=6,events=[(i*.05,{'2':127*i/120}) for i in range(121)]),
      dict(name='algorithm_changes',parameters=patch(p15=127,p5=80),note=52,seconds=6,off=5,events=[(i*.04,{'1':16*(i%8)}) for i in range(125)]),
      dict(name='high_note_max_feedback',parameters=patch(p1=80,p2=127,p4=127,p5=127,p11=0,p12=127,p15=127),note=115,seconds=3,off=2),
    ]

def main():
    OUT.mkdir(parents=True,exist_ok=True);identity=source_identity();report={}
    for fixture in fixtures():
        d=dict(fixture);name=d.pop('name');params=d.pop('parameters')
        diagnostics={}
        audio=render(params,**d,diagnostics=diagnostics);stats=metrics(audio);stats.update(diagnostics)
        assert stats['nonfinite']==0 and stats['peak']<=.950001
        write_wav(OUT/(name+'.wav'),audio)
        meta=dict(fixture,seed=SEED,sample_rate=FS,block_size=12,velocity=.85,identity=identity,metrics=stats,
                  wav_sha256=hashlib.sha256((OUT/(name+'.wav')).read_bytes()).hexdigest(),listening='pending')
        (OUT/(name+'.json')).write_text(json.dumps(meta,indent=2)+'\n');report[name]=stats
        # Standard supported file syntax, integer controls only (sweeps live in fixture metadata).
        preset=HERE/'presets'/(name[:13]+'.txt')
        assert preset.read_text()==''.join(f'P{i+1} {int(v)}\n' for i,v in enumerate(params))
        print(name,json.dumps(stats))
    (OUT/'metrics.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
