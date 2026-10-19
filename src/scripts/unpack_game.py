"""Build a resumable reference tree of DCX assets and BND4 members.
Original game files are read-only. Generated assets are never installed as mods.
Format reference: SoulsFormats/Binder/{Binder,BinderFileHeader}.cs (MIT).
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sqlite3
import struct
import time
import uuid
import zlib

ROOT=Path(__file__).resolve().parents[2]
VERSION=2
MAX_ASSET=1024*1024*1024

def sha(data):return hashlib.sha256(data).hexdigest()

def inflate(data,expected=None):
    if expected is not None and not 0<=expected<=MAX_ASSET:raise ValueError('Oversized declared asset')
    cap=expected if expected is not None else MAX_ASSET
    stream=zlib.decompressobj()
    result=stream.decompress(data,cap+1)
    if len(result)>cap or stream.unconsumed_tail or not stream.eof:raise ValueError('Invalid/truncated/oversized zlib stream')
    if expected is not None and len(result)!=expected:raise ValueError('Decompressed size mismatch')
    if stream.unused_data.strip(b'\0'):raise ValueError('Unexpected data after compressed stream')
    return result

def dcx(data):
    if len(data)<76 or data[:4]!=b'DCX\0' or data[40:44]!=b'DFLT':raise ValueError('Unsupported DCX; expected DFLT')
    expected,packed=struct.unpack_from('>II',data,28)
    if packed>len(data)-76:raise ValueError('Truncated DCX payload')
    return inflate(data[76:76+packed],expected)

def reverse_bits(n):return int(f'{n:08b}'[::-1],2)

def binder(data):
    """Yield (index, ID, original name, uncompressed payload), bounded BND4."""
    if len(data)<64 or data[:4]!=b'BND4':raise ValueError('Not BND4')
    endian='>' if data[9] else '<';bit_big=not data[10]
    raw=data[49];fmt=raw if bit_big or (raw&1 and not raw&128) else reverse_bits(raw)
    if fmt&1:endian='>'
    count=struct.unpack_from(endian+'I',data,12)[0]
    header_size=struct.unpack_from(endian+'Q',data,16)[0]
    stride=struct.unpack_from(endian+'Q',data,32)[0]
    wide=bool(fmt&16);compression=bool(fmt&32);ids=bool(fmt&2);names=bool(fmt&12)
    expected=16+(8 if wide else 4)+(8 if compression else 0)+(4 if ids else 0)+(4 if names else 0)+(8 if fmt==4 else 0)
    if header_size!=64 or stride!=expected or count>100000 or 64+count*stride>len(data):raise ValueError('Invalid BND4 directory')
    for i in range(count):
        at=64+i*stride;flags=data[at] if bit_big else reverse_bits(data[at])
        if data[at+1:at+4]!=b'\0\0\0' or data[at+4:at+8]!=b'\xff'*4:raise ValueError('Invalid BND4 file header')
        stored=struct.unpack_from(endian+'Q',data,at+8)[0];pos=at+16
        size=None
        if compression:size=struct.unpack_from(endian+'Q',data,pos)[0];pos+=8
        offset=struct.unpack_from(endian+('Q' if wide else 'I'),data,pos)[0];pos+=8 if wide else 4
        ident=-1
        if ids:ident=struct.unpack_from(endian+'i',data,pos)[0];pos+=4
        name=f'file_{ident}.bin'
        if names:
            name_at=struct.unpack_from(endian+'I',data,pos)[0]
            if not 64+count*stride<=name_at<len(data):raise ValueError('BND4 name out of bounds')
            step=2 if data[48] else 1;end=name_at
            while end+step<=len(data) and data[end:end+step]!=b'\0'*step:
                end+=step
                if end-name_at>8192:raise ValueError('Oversized BND4 name')
            if end+step>len(data):raise ValueError('Unterminated BND4 name')
            name=data[name_at:end].decode(('utf-16-be' if endian=='>' else 'utf-16-le') if step==2 else 'shift_jis',errors='replace')
        if stored>MAX_ASSET or offset<64+count*stride or offset+stored>len(data):raise ValueError('BND4 data out of bounds')
        payload=data[offset:offset+stored]
        if flags&1:
            payload=dcx(payload) if payload[:4]==b'DCX\0' else inflate(payload,size)
            if size is not None and len(payload)!=size:raise ValueError('BND4 inflated size mismatch')
        yield i,ident,name,payload

def leaf_name(index,ident,name):
    # Binder paths often contain development-machine absolute paths. Keep the
    # original in the index; never use it as a destination path.
    leaf=name.replace('\\','/').rsplit('/',1)[-1]
    leaf=re.sub(r'[<>:"/\\|?*\x00-\x1f]','_',leaf).rstrip(' .') or 'asset.bin'
    if len(leaf)>110:
        p=Path(leaf);leaf=p.stem[:75]+'_'+sha(leaf.encode())[:12]+p.suffix[:12]
    return leaf

def member_names(entries):
    """Use original basenames; retain entry identity only for name collisions."""
    names=[leaf_name(index,ident,name) for index,ident,name in entries]
    # DCX members lose their final extension when expanded.
    keys=[(n[:-4] if n.lower().endswith('.dcx') else n).casefold() for n in names]
    counts=Counter(keys)
    result=[f'{index:05d}_{ident}_{name}' if counts[key]>1 else name
            for (index,ident,_),name,key in zip(entries,names,keys)]
    final=[(n[:-4] if n.lower().endswith('.dcx') else n).casefold() for n in result]
    if len(final)!=len(set(final)):raise ValueError('Ambiguous archive member names')
    return result

def write_asset(root,rel,data,origin,outputs):
    # Resolve an existing parent, not a missing path while another worker is
    # creating the same directory (Windows can return a transient partial path).
    root=root.resolve()
    path=Path(os.path.abspath(root/rel))
    if not path.is_relative_to(root):raise ValueError('Output path escapes reference tree')
    ancestor=path.parent
    while not ancestor.exists():ancestor=ancestor.parent
    if not ancestor.resolve().is_relative_to(root):raise ValueError('Existing output ancestor escapes reference tree')
    path.parent.mkdir(parents=True,exist_ok=True)
    parent=path.parent.resolve()
    if not parent.is_relative_to(root):raise ValueError('Output parent escapes reference tree')
    path=parent/path.name
    if path.exists() and not path.resolve().is_relative_to(root):raise ValueError('Existing output escapes reference tree')
    digest=sha(data)
    if path.exists():
        if not path.is_file() or sha(path.read_bytes())!=digest:raise ValueError(f'Existing output differs; preserved: {path}')
    else:
        if shutil.disk_usage(root).free<len(data)+5*1024**3:raise OSError('Less than 5 GiB reserve space remains')
        temp=path.with_name(path.name+'.'+uuid.uuid4().hex+'.tmp')
        try:
            temp.write_bytes(data);os.replace(temp,path)
        finally:temp.unlink(missing_ok=True)
    st=path.stat()
    outputs.append(dict(path=path.relative_to(root).as_posix(),bytes=len(data),sha256=digest,mtime_ns=st.st_mtime_ns,origin=origin))
    return path

def expand(data,rel,origin,root,outputs,warnings,depth=0):
    if depth>6:raise ValueError('Nested archive limit exceeded')
    if data[:4]==b'DCX\0':
        data=dcx(data)
        rel=rel.with_suffix('') if rel.suffix.lower()=='.dcx' else rel.with_name(rel.name+'.decompressed')
    path=write_asset(root,rel,data,origin,outputs)
    if data[:4]==b'BND4':
        folder=rel.with_name(rel.name+'.unpacked')
        try:
            entries=[(index,ident,name) for index,ident,name,_ in binder(data)]
            names=member_names(entries)
            for (index,ident,name,payload),leaf in zip(binder(data),names):
                member_origin=origin+'!'+name
                expand(payload,folder/leaf,member_origin,root,outputs,warnings,depth+1)
        except Exception as e:warnings.append(f'{rel.as_posix()}: {e}')
    return path

def process(source,rel,output):
    path=source/rel;before=path.stat();outputs=[];warnings=[]
    try:
        data=path.read_bytes();source_hash=sha(data)
        expand(data,Path('dvdroot_ps4')/rel,rel.as_posix(),output,outputs,warnings)
        after=path.stat()
        if (before.st_size,before.st_mtime_ns)!=(after.st_size,after.st_mtime_ns):raise ValueError('Source changed while reading')
        return dict(source=rel.as_posix(),size=before.st_size,mtime=before.st_mtime_ns,sha256=source_hash,status='partial' if warnings else 'done',outputs=outputs,warnings=warnings)
    except Exception as e:return dict(source=rel.as_posix(),size=before.st_size,mtime=before.st_mtime_ns,sha256='',status='error',outputs=outputs,warnings=[str(e)])

def cached(record,source,output):
    if record['status']!='done':return False
    st=source.stat()
    if (st.st_size,st.st_mtime_ns)!=(record['size'],record['mtime']):return False
    for asset in record['outputs']:
        p=(output/asset['path']).resolve()
        if not p.is_relative_to(output) or not p.is_file():return False
        st=p.stat()
        if (st.st_size,st.st_mtime_ns)!=(asset['bytes'],asset['mtime_ns']):return False
    return True

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--output',type=Path,default=ROOT/'game-unpacked/raw')
    p.add_argument('--workers',type=int,default=4)
    p.add_argument('--limit',type=int)
    p.add_argument('--mode',choices=['full','scripts'],default='full')
    args=p.parse_args();source=args.source.resolve();output=args.output.resolve()
    if not source.is_dir() or output==source or output.is_relative_to(source) or source.is_relative_to(output):p.error('Source and output must be separate trees')
    if not output.is_relative_to(ROOT):p.error('Reference output must be inside this workspace')
    output.mkdir(parents=True,exist_ok=True)
    lock=output/'.unpack.lock'
    try:fd=os.open(lock,os.O_CREAT|os.O_EXCL|os.O_WRONLY)
    except FileExistsError:p.error(f'Extraction already running, or stale lock after interruption: {lock}')
    os.write(fd,str(os.getpid()).encode());os.close(fd)
    db=None
    try:
        db=sqlite3.connect(output/'index.sqlite')
        db.execute('CREATE TABLE IF NOT EXISTS metadata (key TEXT PRIMARY KEY,value TEXT)')
        db.execute('CREATE TABLE IF NOT EXISTS sources (path TEXT PRIMARY KEY,record TEXT NOT NULL)')
        existing=dict(db.execute('SELECT key,value FROM metadata'))
        expected={'source':str(source),'version':str(VERSION),'mode':args.mode}
        if existing and existing!=expected:raise ValueError('Reference index belongs to another source/tool version')
        db.executemany('INSERT OR REPLACE INTO metadata VALUES (?,?)',expected.items());db.commit()
        source_files=sorted((f for f in source.rglob('*') if f.is_file()))
        with (output/'source-files.csv').open('w',encoding='utf-8-sig',newline='') as f:
            writer=csv.writer(f);writer.writerow(['relative_path','source_path','bytes','handling'])
            for asset in source_files:
                kind='decompress' if asset.suffix.lower()=='.dcx' else 'paired archive; retained at source' if asset.suffix.lower() in ('.tpfbhd','.tpfbdt','.hkxbhd','.hkxbdt') else 'already uncompressed/opaque; retained at source'
                writer.writerow([asset.relative_to(source).as_posix(),str(asset),asset.stat().st_size,kind])
        paths=[f.relative_to(source) for f in source_files if f.suffix.lower()=='.dcx' and (args.mode=='full' or f.relative_to(source).parts[0] in ('script','action','ai'))]
        if args.limit is not None:paths=paths[:max(0,args.limit)]
        done={path:json.loads(record) for path,record in db.execute('SELECT path,record FROM sources')}
        todo=[];skipped=0
        for rel in paths:
            if rel.as_posix() in done and cached(done[rel.as_posix()],source/rel,output):skipped+=1
            else:todo.append(rel)
        print(f'{len(paths)} DCX sources; {skipped} already extracted; {len(todo)} remaining. Output: {output}',flush=True)
        start=time.monotonic();processed=0;last=start;counts=Counter()
        with ThreadPoolExecutor(max_workers=min(8,max(1,args.workers))) as executor:
            iterator=iter(todo);pending={}
            def submit():
                try:rel=next(iterator)
                except StopIteration:return
                pending[executor.submit(process,source,rel,output)]=rel
            for _ in range(min(8,max(1,args.workers))*2):submit()
            while pending:
                finished,_=wait(pending,timeout=2,return_when=FIRST_COMPLETED)
                for future in finished:
                    rel=pending.pop(future);record=future.result();done[rel.as_posix()]=record
                    db.execute('INSERT OR REPLACE INTO sources VALUES (?,?)',(record['source'],json.dumps(record,ensure_ascii=False)));db.commit()
                    processed+=1;counts[record['status']]+=1
                    if record['warnings']:print(record['status'].upper()+': '+record['source']+' '+str(record['warnings'][:2]),flush=True)
                    submit()
                now=time.monotonic()
                if now-last>=10 or not pending:
                    progress=dict(total=len(paths),completed=skipped+processed,processed=processed,cached=skipped,elapsed_seconds=round(now-start,1),statuses=dict(counts))
                    temp=output/'progress.json.tmp';temp.write_text(json.dumps(progress,indent=2));os.replace(temp,output/'progress.json')
                    print(f'{skipped+processed}/{len(paths)} sources | {dict(counts)} | {now-start:.0f}s',flush=True);last=now
        all_records=[done[r.as_posix()] for r in paths];assets=[a for r in all_records for a in r['outputs']]
        summary=dict(source=str(source),output=str(output),dcx_sources=len(paths),complete=args.limit is None and all(r['status']=='done' for r in all_records),statuses=dict(Counter(r['status'] for r in all_records)),output_files=len(assets),output_bytes=sum(a['bytes'] for a in assets),extensions=dict(Counter(Path(a['path']).suffix.lower() for a in assets)),source_files_modified=False,elapsed_seconds=round(time.monotonic()-start,1))
        (output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
        with (output/'index.csv').open('w',encoding='utf-8-sig',newline='') as f:
            writer=csv.DictWriter(f,fieldnames=['source','origin','output','bytes','sha256']);writer.writeheader()
            for record in all_records:
                for a in record['outputs']:writer.writerow(dict(source=record['source'],origin=a['origin'],output=a['path'],bytes=a['bytes'],sha256=a['sha256']))
        print(json.dumps(summary,indent=2),flush=True)
        if any(r['status']!='done' for r in all_records):raise SystemExit(1)
    finally:
        if db:db.close()
        lock.unlink(missing_ok=True)

if __name__=='__main__':main()
