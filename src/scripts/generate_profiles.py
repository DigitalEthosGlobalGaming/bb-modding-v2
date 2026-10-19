"""Verify and generate private compatibility checks from the user's unencrypted SELF/ELF."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from decompile_lua import atomic

ROOT=Path(__file__).resolve().parents[2]

def segments(data):
    self_entries=None;elf_offset=0
    if data[:4]==bytes.fromhex('4f153d1d'):
        if len(data)<32:raise ValueError('Truncated SELF header')
        count=struct.unpack_from('<H',data,24)[0]
        if count>4096 or 32+count*32+64>len(data):raise ValueError('Invalid SELF segment table')
        self_entries=[struct.unpack_from('<QQQQ',data,32+i*32) for i in range(count)]
        elf_offset=32+count*32
    if data[elf_offset:elf_offset+6]!=b'\x7fELF\x02\x01':raise ValueError('Expected little-endian ELF64 or unencrypted SELF')
    phoff=struct.unpack_from('<Q',data,elf_offset+32)[0]
    size,count=struct.unpack_from('<HH',data,elf_offset+54)
    if size!=56 or count>4096 or elf_offset+phoff+count*size>len(data):raise ValueError('Invalid ELF program headers')
    headers=[struct.unpack_from('<IIQQQQQQ',data,elf_offset+phoff+i*size) for i in range(count)]
    result=[]
    for index,ph in enumerate(headers):
        kind,_,offset,address,_,filesize,_,_=ph
        if kind!=1 or not filesize:continue
        if self_entries is None:body=data[offset:offset+filesize]
        else:
            matches=[e for e in self_entries if (e[0]>>20)&0xfff==index and e[0]&0x800]
            if len(matches)!=1:raise ValueError(f'Unsupported SELF layout for ELF segment {index}')
            flags,offset,packed,unpacked=matches[0]
            if flags&2:raise ValueError('Encrypted SELF is unsupported; no keys or bypasses are provided')
            if offset+packed>len(data):raise ValueError('Truncated SELF segment')
            body=data[offset:offset+packed]
            if flags&8:
                dec=zlib.decompressobj();body=dec.decompress(body,filesize+1)
                if len(body)>filesize or dec.unconsumed_tail:raise ValueError('Oversized SELF segment')
        if len(body)!=filesize:raise ValueError('ELF segment size mismatch')
        result.append((address,body))
    return result

def generate(path):
    parts=segments(path.read_bytes())
    profiles=json.loads(Path(__file__).with_name('profiles.lock.json').read_text())
    outputs={}
    for name,checks in profiles.items():
        rows=[]
        for check in checks:
            address=check['rva'];length=check['length']
            candidates=[body[address-start:address-start+length] for start,body in parts if start<=address and address+length<=start+len(body)]
            if len(candidates)!=1 or hashlib.sha256(candidates[0]).hexdigest()!=check['sha256']:
                raise ValueError(f'Unsupported executable fingerprint: {name} +{check["rva"]:x}')
            payload=','.join(hex(b) for b in candidates[0]);prefix=f'0x{check["rva"]:x},'
            if name=='character_profile':prefix+=str(length)+','
            rows.append('{'+prefix+'{'+payload+'}},')
        declaration=('static const struct character_check {uintptr_t rva;size_t n;unsigned char bytes[32];} character_checks[] = {' if name=='character_profile'
                     else 'static const struct {uintptr_t rva;unsigned char bytes[20];} menu_signatures[] = {')
        outputs[name]='/* Generated locally from user game; never redistribute. */\n'+declaration+'\n'+'\n'.join(rows)+'\n};\n'
    for name,text in outputs.items():atomic(ROOT/'src/host/src'/f'{name}.local.h',text.encode())
    print('Verified and generated local character/native-menu profiles. No executable bytes were added to tracked source.')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('executable',type=Path)
    generate(parser.parse_args().executable)
