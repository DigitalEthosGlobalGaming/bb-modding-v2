"""Check prospective Git contents for known private outputs; not a legal clearance tool."""
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
PRIVATE={'tools','game-unpacked','mods','.local','target','build','dist'}
EXTENSIONS={'.pkg','.bin','.elf','.dcx','.bnd','.gfx','.hks','.luac','.lua','.exe','.dll','.zip','.nupkg','.pdb','.sqlite'}

def reasons(relative):
    p=Path(relative);parts=p.parts;problems=[]
    if parts[0] in PRIVATE or 'local-data' in parts or p.name.endswith('.local.h'):problems.append('private/generated path')
    if p.suffix.lower() in EXTENSIONS:problems.append('binary/game/tool output extension')
    if p.name.endswith('_generated.rs') or p.name in ('items.json','characters.json','character-references.json','bbdata.rs'):problems.append('generated game catalog/schema')
    target=ROOT/p
    if target.is_symlink():problems.append('symlink publication requires explicit provenance review')
    if target.is_file():
        if target.stat().st_size>2*1024*1024:problems.append('large source file requires provenance review')
        if target.stat().st_size<2*1024*1024:
            data=target.read_bytes()
            if (b'C:' + b'\\games\\bloodborne-pc') in data or (b'C:' + b'/games/bloodborne-pc') in data:problems.append('developer machine absolute path')
    return problems

def main():
    listing=subprocess.check_output(['git','-C',str(ROOT),'ls-files','--cached','--others','--exclude-standard','-z'])
    files=sorted(set(name.decode('utf-8') for name in listing.split(b'\0') if name))
    failures=[]
    for path in files:
        for reason in reasons(path):failures.append(f'{path}: {reason}')
    if failures:
        print('\n'.join(failures),file=sys.stderr);return 1
    print(f'PASS: {len(files)} publication candidates; known private outputs excluded. Manual provenance review still required.')
    return 0

if __name__=='__main__':raise SystemExit(main())
