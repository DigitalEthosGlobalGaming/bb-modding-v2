"""Read binary field layouts from locally downloaded Smithbox PARAMDEF XML.

Descriptions, display names and default game values are not exported.
"""
import re
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[2]
DEFS=ROOT/'tools/Smithbox/2.2.6/Assets/PARAM/BB/Defs'
SIZES={'s8':1,'u8':1,'s16':2,'u16':2,'s32':4,'u32':4,'f32':4,'dummy8':1,'fixstr':1,'fixstrW':2}

def fields_schema():
    if not DEFS.is_dir():raise ValueError('Install Smithbox before compiling content')
    schemas={}
    for path in sorted(DEFS.glob('*.xml')):
        xml=ET.parse(path).getroot();offset=0;bit=0;unit=0;fields={}
        for field in xml.findall('./Fields/Field'):
            definition=field.attrib['Def'].split('=',1)[0].strip()
            match=re.fullmatch(r'(\w+)\s+(\w+)(?:\[(\d+)\])?(?:\s*:\s*(\d+))?',definition)
            if not match:raise ValueError(f'Unsupported PARAMDEF declaration: {path.name}: {definition}')
            ty,name,count,bits=match.groups();size=SIZES.get(ty)
            if not size:raise ValueError(f'Unsupported PARAMDEF type: {ty}')
            count=int(count or '1');bits=int(bits or '0')
            if bits:
                if count!=1 or bits>size*8:raise ValueError('Invalid bit field')
                if unit and (unit!=size or bit+bits>unit*8):offset+=unit;unit=0;bit=0
                unit=size
                fields[name]={'ty':ty.upper(),'offset':offset,'bit':bit,'bits':bits}
                bit+=bits
                if bit==unit*8:offset+=unit;bit=0;unit=0
            else:
                if unit:offset+=unit;unit=0;bit=0
                if count==1 and ty not in ('dummy8','fixstr','fixstrW'):
                    fields[name]={'ty':ty.upper(),'offset':offset,'bit':0,'bits':0}
                offset+=size*count
        if unit:offset+=unit
        schemas[path.stem]=(offset,fields)
    if not schemas:raise ValueError('No local PARAMDEF schemas')
    return schemas
