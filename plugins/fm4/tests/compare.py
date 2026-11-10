#!/usr/bin/env python3
"""Compare optimized audio with an explicitly supplied native 0.1.0 library. MIT."""
import argparse, hashlib, json
from pathlib import Path
from render import render, DEFAULT, HERE
parser=argparse.ArgumentParser();parser.add_argument('baseline',type=Path);args=parser.parse_args()
result={'baseline_library_sha256':hashlib.sha256(args.baseline.read_bytes()).hexdigest(),'current_library_sha256':hashlib.sha256((HERE/'libfm4.dylib').read_bytes()).hexdigest(),'cases':[]}
for algorithm in range(8):
    p=DEFAULT.copy();p[0]=16*algorithm;p[14]=127;p[3]=127;p[4]=127;p[10]=0;p[11]=127
    events=[(.1*i,{'1':16*((algorithm+i)%8),'2':(i*17)%128,'3':(i*31)%128,'6':(i*11)%128,'7':(i*19)%128,'8':(i*13)%128,'9':(i*29)%128,'10':(i*23)%128,'11':(i*7)%128,'14':(i*37)%128}) for i in range(20)]
    a=render(p,72,2.5,off=2,events=events,libpath=args.baseline.resolve())
    b=render(p,72,2.5,off=2,events=events)
    diffs=[abs(x-y) for x,y in zip(a,b)]
    row={'algorithm':algorithm,'samples':len(a),'different_samples':sum(x!=0 for x in diffs),'maximum_difference':max(diffs)}
    result['cases'].append(row);print(row)
    assert row['different_samples']==0,'Audio changed; investigate numerical compatibility before shipping'
(HERE/'build/optimized-equivalence.json').write_text(json.dumps(result,indent=2)+'\n')
