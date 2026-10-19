"""Generate local NPC and action lookups from the supplied game. No schema dump required."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from archive_formats import unpack_dcx,bnd4_files,read_fmg
from decompile_lua import atomic

ROOT=Path(__file__).resolve().parents[2]

def rs(value):
    return re.sub(r'\\u([0-9a-fA-F]{4})',r'\\u{\1}',json.dumps(value,ensure_ascii=False))

def action_table(path):
    text=path.read_bytes().decode('cp932')
    counts=re.findall(r'^Num\s*=\s*(\d+)\s*$',text,re.M)
    rows=[(int(i),n) for i,n in re.findall(r'^\s*(\d+)\s*=\s*"([^"\r\n]+)"\s*$',text,re.M)]
    if len(counts)!=1 or len(rows)!=int(counts[0]) or [i for i,_ in rows]!=list(range(1,len(rows)+1)):
        raise ValueError(f'Unsupported action table: {path}')
    return [{'id':i,'name':n} for i,n in rows]

def generate(game):
    destination=ROOT/'sdk/rust/bbmod/local-data';destination.mkdir(parents=True,exist_ok=True)
    params=unpack_dcx((game/'param/gameparam/gameparam.parambnd.dcx').read_bytes())
    tables=[params[o:o+n] for _,o,n,_ in bnd4_files(params) if params[o+12:o+44].split(b'\0')[0]==b'NPC_PARAM_ST']
    if len(tables)!=1:raise ValueError('Expected one NPC parameter table')
    data=tables[0];count=struct.unpack_from('<H',data,10)[0]
    if count<2 or len(data)<64+count*24:raise ValueError('Truncated NPC rows')
    # Three 1.09 binary-layout offsets; no imported Inspector/Openborne schema.
    records=[struct.unpack_from('<iIQQ',data,64+i*24) for i in range(count)]
    if records[1][2]-records[0][2]!=388:raise ValueError('Unsupported NPC row size')
    msg=unpack_dcx((game/'msg/engus/item.msgbnd.dcx').read_bytes())
    fmgs=[read_fmg(msg[o:o+n]) for _,o,n,i in bnd4_files(msg) if i==18]
    if len(fmgs)!=1:raise ValueError('Missing NPC names')
    characters=[]
    for ident,_,offset,name_offset in records:
        if ident<0 or offset<64+count*24 or offset+388>len(data):raise ValueError('Invalid NPC row')
        internal=None
        if name_offset:
            if name_offset>=len(data) or name_offset%2:raise ValueError('Invalid NPC string')
            end=name_offset
            while end+2<=len(data) and data[end:end+2]!=b'\0\0':
                end+=2
                if end-name_offset>8192:raise ValueError('Oversized NPC string')
            if end+2>len(data):raise ValueError('Unterminated NPC string')
            internal=data[name_offset:end].decode('utf-16-le').strip() or None
        name_id=struct.unpack_from('<i',data,offset+8)[0];raw=fmgs[0].get(name_id);name=raw.strip() if raw else None
        characters.append(dict(id=ident,key=f'bloodborne.npc_{ident}',name_id=name_id,name=name,internal_name=internal,
            base_health=struct.unpack_from('<I',data,offset+32)[0],team_type=data[offset+303]))
    characters.sort(key=lambda c:c['id'])
    if len({c['id'] for c in characters})!=len(characters):raise ValueError('Duplicate NPC IDs')
    lines=['pub static DEFINITIONS: &[CharacterDefinition] = &[']
    option=lambda s:'None' if s is None else f'Some({rs(s)})'
    for c in characters:
        status='Missing' if not c['name'] else 'Placeholder' if c['name'] in ('*','NOT NEEDED') else 'Named'
        lines.append(f'CharacterDefinition {{ id:NpcParamId({c["id"]}),key:{rs(c["key"])},name_id:{c["name_id"]},name:{option(c["name"])},description:None,internal_name:{option(c["internal_name"])},reference_name:None,name_status:NameStatus::{status},base_health:{c["base_health"]},team_type:{c["team_type"]} }},')
    lines+= ['];','static NAME_INDEX: &[usize] = &['+','.join(map(str,sorted(range(len(characters)),key=lambda i:characters[i]['name_id'])))+'];']
    atomic(destination/'characters.json',(json.dumps({'characters':characters},ensure_ascii=False,indent=2)+'\n').encode())
    atomic(destination/'character_definitions_generated.rs',('\n'.join(lines)+'\n').encode())
    references={k:action_table(game/'action'/v) for k,v in [('states','statenameid.txt'),('events','eventnameid.txt'),('variables','variablenameid.txt')]}
    # Numeric labels deliberately avoid redistributing or inventing community semantic names.
    lines=['pub static TEAMS: &[TeamDefinition] = &[];']
    for key,values in references.items():
        lines.append(f'pub static {key.upper()}: &[ActionName] = &[')
        lines += [f'ActionName {{id:{v["id"]},name:{rs(v["name"])}}},' for v in values]
        lines.append('];')
    candidates=sorted((int(v['name'][5:]),v) for v in references['states'] if re.fullmatch(r'Event\d+',v['name']))
    if len({n for n,_ in candidates})!=len(candidates):raise ValueError('Ambiguous lexical action candidates')
    lines.append('pub static SCRIPT_CANDIDATES: &[ScriptCandidate] = &[')
    lines += [f'ScriptCandidate {{value:ScriptValue({n}),action_state:ActionStateId({v["id"]}),name:{rs(v["name"])}}},' for n,v in candidates]
    lines.append('];')
    atomic(destination/'character-references.json',(json.dumps(references,ensure_ascii=False,indent=2)+'\n').encode())
    atomic(destination/'character_references_generated.rs',('\n'.join(lines)+'\n').encode())
    print(f'Generated {len(characters)} local NPC definitions and action dictionaries. Team/reference labels are unavailable.')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--game',type=Path,required=True)
    generate(parser.parse_args().game)
