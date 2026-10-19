"""Create separate UTF-8 reference sources, preserving raw extraction and bytecode."""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def digest(data):
    return hashlib.sha256(data).hexdigest()

def atomic(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as f:
        temporary = Path(f.name)
        f.write(data)
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)

def decompile(source, destination, dll, old):
    data = source.read_bytes()
    record = {'input_sha256': digest(data)}
    if destination.is_file():
        current = digest(destination.read_bytes())
        if old and old.get('input_sha256') == record['input_sha256'] and old.get('output_sha256') == current:
            return old
        raise ValueError(f'Existing reference changed or has no provenance; preserved: {destination}')
    if not data.startswith(b'\x1bLua'):
        if b'\0' in data:
            return dict(record, status='unsupported_binary')
        data.decode('utf-8-sig')  # fail visibly for an unrecognised source encoding
        atomic(destination, data)
        return dict(record, status='already_text', output_sha256=digest(data))
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as temp:
        output = Path(temp)/'output.lua'
        result = subprocess.run(['dotnet', str(dll), str(source), '-o', str(output)], capture_output=True, timeout=180)
        if result.returncode or not output.is_file():
            return dict(record, status='failed', error=(result.stdout+result.stderr).decode('utf-8',errors='replace')[-4000:])
        text = output.read_bytes().decode('cp932' if data[4] == 0x50 else 'utf-8-sig')
        if not text.strip() or '\0' in text or text.startswith('\x1bLua'):
            return dict(record, status='failed', error='Output failed text sanity check')
        encoded = text.encode('utf-8')
        atomic(destination, encoded)
        return dict(record, status='decompiled', output_sha256=digest(encoded))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--raw', type=Path, default=ROOT/'game-unpacked/raw')
    parser.add_argument('--output', type=Path, default=ROOT/'game-unpacked/decompiled')
    parser.add_argument('--dll', type=Path, default=ROOT/'tools/DSLuaDecompiler/DSLuaDecompiler/bin/Release/net9.0/DSLuaDecompiler.dll')
    parser.add_argument('--workers', type=int, default=4)
    args = parser.parse_args()
    source, raw, output = (p.resolve() for p in (args.source,args.raw,args.output))
    if not source.is_dir() or not raw.is_dir() or not args.dll.is_file():
        parser.error('Game source, raw extraction and built DSLuaDecompiler are required')
    for parent in (source,raw):
        if parent==output or parent.is_relative_to(output) or output.is_relative_to(parent):
            parser.error('Decompiled output must be separate from game and raw extraction')
    if not output.is_relative_to(ROOT/'game-unpacked'):
        parser.error('Output must be inside toolkit game-unpacked')
    output.mkdir(parents=True,exist_ok=True)
    lock=output/'.decompile.lock'
    fd=os.open(lock,os.O_CREAT|os.O_EXCL|os.O_WRONLY)
    os.close(fd)
    try:
        # Loose source scripts and archive members retain their original relative names.
        scripts={p.relative_to(source):p for p in source.rglob('*') if p.is_file() and p.suffix.lower() in ('.lua','.luac','.hks')}
        for p in (raw/'dvdroot_ps4').rglob('*'):
            if p.is_file() and p.suffix.lower() in ('.lua','.luac','.hks'):
                rel=p.relative_to(raw/'dvdroot_ps4')
                if rel in scripts and digest(scripts[rel].read_bytes())!=digest(p.read_bytes()):
                    raise ValueError(f'Ambiguous script source: {rel}')
                scripts[rel]=p
        report=output/'report.json'
        previous=json.loads(report.read_text(encoding='utf-8'))['files'] if report.exists() else {}
        records={}
        def save():
            atomic(report,(json.dumps({'complete':len(records)==len(scripts) and all(r['status'] in ('decompiled','already_text') for r in records.values()),'files':records},indent=2)+'\n').encode())
        def work(pair):
            rel,p=pair; key=rel.as_posix()
            try:
                dest=output/'dvdroot_ps4'/rel
                if not dest.resolve().is_relative_to(output):raise ValueError('Output escapes reference tree')
                result=decompile(p,dest,args.dll,previous.get(key))
            except Exception as e:
                result={'status':'failed','error':str(e)}
            return key,result
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,min(args.workers,8))) as pool:
            for key,record in pool.map(work,sorted(scripts.items())):
                records[key]=record
                if len(records)%25==0:save();print(f'{len(records)}/{len(scripts)} scripts',flush=True)
        save()
        failed=sum(r['status'] not in ('decompiled','already_text') for r in records.values())
        print(f'{len(records)} scripts; {failed} failed/unsupported. Report: {report}')
        if failed:raise SystemExit(1)
    finally:
        lock.unlink(missing_ok=True)

if __name__=='__main__':main()
