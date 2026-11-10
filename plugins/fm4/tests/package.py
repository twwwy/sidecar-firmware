#!/usr/bin/env python3
"""Manifest plus optional package validation using the repository's real packer."""
import json, pathlib, sys, zlib, struct
here=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(here.parents[1]/'tools'))
import rmp_pack as pack
m=json.loads((here/'manifest.json').read_text())
pack._manifest(m)
assert len(m['knobs'])==16 and [k['p'] for k in m['knobs']]==list(range(1,17))
assert m['page2']=='params' and m['id']=='fm4.pm4' and m['max_voices']==4 and m['version']=='0.1.1' and m['compatible_from']=='0.1.0'
assert pack.package_format(m)==3
p=here/'fm4.a64.rmp'
if p.exists():
    elf=here/'fm4.a64.elf'
    for source in [*here.glob('*.c'),*here.glob('*.h')]:
        assert source.stat().st_mtime<=elf.stat().st_mtime, f'Package stale relative to {source.name}; rebuild a64'
    data=p.read_bytes(); h=pack.parse_header(data)
    assert h['flags']==pack.header_flags(m) and h['version']==0x000101
    assert h['magic']==0x41504d52 and h['api']==2 and h['format']==3
    assert h['crc32']==zlib.crc32(data[96:])&0xffffffff
    assert pack.parse_manifest(data)['name']=='FM4'
    magic,api,state,cpu=struct.unpack_from('<IIII',data,96+h['entry_offset'])
    assert magic==0x504d5252 and api==2 and state<4096 and cpu==6322
    assert data==pack.pack(str(here/'fm4.a64.elf'),str(here/'manifest.json'))
    print('Package PASS:',h['id'],'state',state,'cpu provisional',cpu,'image',h['image_size'],'bss',h['bss_size'])
else: print('Manifest PASS; package not built yet')
