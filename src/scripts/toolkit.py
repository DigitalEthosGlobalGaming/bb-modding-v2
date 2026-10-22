"""Portable local workspace setup. Game inputs and installed runtime are read-only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
LOCK = json.loads((Path(__file__).with_name('tools.lock.json')).read_text())
CONFIG = ROOT/'.local/workspace.json'

def run(*arguments, cwd=None, env=None):
    print('+', ' '.join(map(str,arguments)), flush=True)
    subprocess.run(list(map(str,arguments)),cwd=cwd,env=env,check=True)

def powershell():
    # Prefer the current PowerShell runtime, keeping its module environment consistent.
    shell=shutil.which('pwsh') or shutil.which('powershell')
    if not shell:raise ValueError('PowerShell is required to run this helper.')
    return shell

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()

def save(path, value):
    from decompile_lua import atomic
    atomic(path,(json.dumps(value,indent=2)+'\n').encode('utf-8'))

def child(root, relative):
    if Path(relative).is_absolute() or ':' in str(relative) or '\\' in str(relative):
        raise ValueError('Expected a relative forward-slash path')
    target=(root/relative).resolve()
    if not target.is_relative_to(root.resolve()) or target==root.resolve():
        raise ValueError(f'Path escapes root: {relative}')
    return target

def sfo(path):
    data=path.read_bytes()
    if len(data)<20 or data[:4]!=b'\0PSF':raise ValueError(f'Invalid PARAM.SFO: {path}')
    keys,values,count=struct.unpack_from('<III',data,8)
    if count>4096 or 20+16*count>len(data):raise ValueError('Invalid SFO index')
    result={}
    for i in range(count):
        key,fmt,size,maximum,offset=struct.unpack_from('<HHIII',data,20+i*16)
        start=keys+key;end=data.find(b'\0',start)
        if end<start or values+offset+size>len(data) or size>maximum:raise ValueError('Invalid SFO entry')
        name=data[start:end].decode('utf-8')
        if fmt==0x204:result[name]=data[values+offset:values+offset+size].rstrip(b'\0').decode('utf-8')
    return result

def validate(game, runtime):
    game,runtime=game.resolve(),runtime.resolve()
    title=game/'CUSA03173/uroot'
    if not title.is_dir():raise ValueError('Select the game folder containing CUSA03173/uroot (not dvdroot_ps4).')
    metadata=sfo(title/'sce_sys/param.sfo')
    if metadata.get('TITLE_ID')!='CUSA03173' or metadata.get('APP_VER')!='01.09':
        raise ValueError(f'Expected CUSA03173 01.09; found {metadata.get("TITLE_ID")} {metadata.get("APP_VER")}')
    assets=title/'dvdroot_ps4'
    for rel in ['param/gameparam/gameparam.parambnd.dcx','msg/engus/item.msgbnd.dcx','action/script/c0000.hks']:
        if not (assets/rel).is_file():raise ValueError(f'Missing required game input: {rel}')
    if not (title/'eboot.bin').is_file():raise ValueError('Missing game eboot.bin')
    if not runtime.is_dir() or not (runtime/'Bloodborne.exe').is_file():
        raise ValueError('Select an installed bbport folder containing Bloodborne.exe')
    probe=runtime/'bin/bb-probe.exe'
    if not probe.is_file():probe=runtime/'bb-probe.exe'
    if not probe.is_file() or probe.open('rb').read(2)!=b'MZ':raise ValueError('Missing/invalid installed bb-probe.exe')
    for input_path in (game,runtime):
        if input_path==ROOT or ROOT.is_relative_to(input_path) or input_path.is_relative_to(ROOT):
            raise ValueError('Game/runtime and toolkit must be separate trees')
    # A string is evidence of integration, not a gameplay or capability test.
    marker=b'bbmods.log' in probe.read_bytes()
    return {'schema_version':1,'game_directory':str(game),'game_assets':str(assets),'runtime_directory':str(runtime),
        'title_id':metadata['TITLE_ID'],'app_version':metadata['APP_VER'],
        'runtime_probe':str(probe),'runtime_sha256':sha(probe),
        'framework_marker_found':marker,'runtime_capabilities_verified':False}

def config():
    if not CONFIG.is_file():raise ValueError('Run setup.ps1 first.')
    value=json.loads(CONFIG.read_text())
    validate(Path(value['game_directory']),Path(value['runtime_directory']))
    return value

def download(url, target, expected):
    target.parent.mkdir(parents=True,exist_ok=True)
    if target.exists() and sha(target)==expected:return
    partial=target.with_suffix(target.suffix+'.partial')
    request=urllib.request.Request(url,headers={'User-Agent':'bb-modding-toolkit/1'})
    try:
        with urllib.request.urlopen(request,timeout=120) as response,partial.open('wb') as output:
            shutil.copyfileobj(response,output)
        if sha(partial)!=expected:raise ValueError(f'Download checksum mismatch: {url}')
        os.replace(partial,target)
    finally:partial.unlink(missing_ok=True)

def extract_zip(archive, destination):
    destination.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            child(destination,info.filename.rstrip('/'))
            if (info.external_attr>>16)&0o170000==0o120000:raise ValueError('ZIP symlinks are not allowed')
        z.extractall(destination)

def checkout(repository, commit, destination):
    if not destination.exists():run('git','-c','core.autocrlf=false','clone','--no-checkout',repository,destination)
    actual=subprocess.check_output(['git','-C',str(destination),'rev-parse','--show-toplevel'],text=True).strip()
    if Path(actual).resolve()!=destination.resolve():raise ValueError('Tool path is not its own Git checkout')
    # Never discard edits in a downloaded tool/source checkout.
    status=subprocess.check_output(['git','-C',str(destination),'status','--porcelain','--untracked-files=no'],text=True)
    if (destination/'.git/index').exists() and status.strip():raise ValueError(f'Downloaded source has changes; preserved: {destination}')
    have=subprocess.run(['git','-C',str(destination),'cat-file','-e',commit+'^{commit}'],capture_output=True)
    if have.returncode:run('git','-C',destination,'fetch','origin',commit)
    run('git','-C',destination,'-c','core.autocrlf=false','checkout','--detach',commit)

def source_archive(name,url,expected,destination):
    archive=ROOT/'tools/downloads'/f'{name}.zip'
    download(url,archive,expected)
    stamp=destination/'.toolkit-source.json'
    if stamp.exists():
        info=json.loads(stamp.read_text())
        if info['sha256']!=expected:raise ValueError(f'Tool source pin changed: {destination}')
        for rel,digest in info['files'].items():
            if not child(destination,rel).is_file() or sha(child(destination,rel))!=digest:
                raise ValueError(f'Tool source changed; preserved: {destination/rel}')
        return
    if destination.exists() and any(destination.iterdir()):raise ValueError(f'Existing tool source is unstamped; preserved: {destination}')
    with tempfile.TemporaryDirectory(dir=archive.parent) as temporary:
        staging=Path(temporary);extract_zip(archive,staging)
        roots=list(staging.iterdir())
        if len(roots)!=1 or not roots[0].is_dir():raise ValueError('Unexpected source archive layout')
        shutil.copytree(roots[0],destination,dirs_exist_ok=True)
    save(stamp,{'url':url,'sha256':expected,'files':{p.relative_to(destination).as_posix():sha(p) for p in destination.rglob('*') if p.is_file()}})

def install_tools(cache=None):
    smith=ROOT/'tools/Smithbox/2.2.6'
    archive=ROOT/'tools/downloads/Smithbox-2.2.6.zip'
    spec=LOCK['smithbox']
    if not (smith/'installed.json').exists():
        # A cache is only reused when its downloaded archive matches the lock hash.
        candidates=list(cache.rglob('*.zip')) if cache and cache.is_dir() else []
        for candidate in candidates:
            if candidate.stat().st_size>250_000_000 and sha(candidate)==spec['sha256']:
                archive.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(candidate,archive);break
        download(spec['url'],archive,spec['sha256'])
        extract_zip(archive,smith)
        executables=list(smith.rglob('Smithbox.Vulkan.exe'))
        if len(executables)!=1:raise ValueError('Smithbox archive layout unexpected')
        save(smith/'installed.json',dict(spec,executable=str(executables[0].relative_to(smith))))
    else:
        recorded=json.loads((smith/'installed.json').read_text())
        if recorded['sha256']!=spec['sha256']:raise ValueError('Installed Smithbox lock mismatch')
    target=ROOT/'tools/DSLuaDecompiler';spec=LOCK['decompiler']
    source_archive('DSLuaDecompiler',spec['archive_url'],spec['archive_sha256'],target)
    souls=target/'SoulsFormats'
    # Clone the exact gitlink commit independently; avoids submodule-helper differences.
    source_archive('SoulsFormats',spec['soulsformats_archive_url'],spec['soulsformats_archive_sha256'],souls)
    save(target/'global.json',{'sdk':{'version':spec['dotnet_sdk'],'rollForward':'latestFeature'}})
    env=os.environ.copy();env.update(DOTNET_CLI_HOME=str(ROOT/'tools/dotnet-home'),DOTNET_CLI_TELEMETRY_OPTOUT='1',DOTNET_GENERATE_ASPNET_CERTIFICATE='false')
    package=spec['nuget'];feed=ROOT/'tools/nuget-feed'
    download(package['url'],feed/package['filename'],package['sha256'])
    run('dotnet','build','DSLuaDecompiler/DSLuaDecompiler.csproj','-c','Release',
        '--source',feed,f'-p:RestorePackagesPath={ROOT/"tools/nuget-packages"}',cwd=target,env=env)
    save(ROOT/'.local/tools.json',LOCK)

def references(mode):
    cfg=config();assets=cfg['game_assets'];out=ROOT/'game-unpacked/raw'
    required=60*1024**3 if mode=='full' else 6*1024**3
    print('Full reference extraction previously used approximately 52 GB; paired/opaque formats remain at source.',flush=True)
    if shutil.disk_usage(ROOT).free<required:raise ValueError(f'Need at least {required//1024**3} GiB free before this extraction mode.')
    run(sys.executable,Path(__file__).with_name('unpack_game.py'),'--source',assets,'--output',out,'--mode',mode)
    run(sys.executable,Path(__file__).with_name('decompile_lua.py'),'--source',assets,'--raw',out)
    run(sys.executable,Path(__file__).with_name('generate_items.py'),'--game',assets)
    run(sys.executable,Path(__file__).with_name('generate_catalogs.py'),'--game',assets)
    run(sys.executable,Path(__file__).with_name('annotate_scripts.py'))
    run(sys.executable,Path(__file__).with_name('generate_profiles.py'),Path(assets).parent/'eboot.bin')
    save(ROOT/'.local/references.json',{'mode':mode,'game':assets,'completed':True})

def new_mod(identifier, name):
    import re
    if not re.fullmatch(r'[a-z][a-z0-9_-]*(\.[a-z][a-z0-9_-]*)+',identifier):raise ValueError('Use a mod ID such as me.my-mod')
    destination=child(ROOT/'mods',identifier)
    if destination.exists():raise ValueError('Mod already exists')
    shutil.copytree(ROOT/'docs/templates/rust-mod',destination)
    crate=identifier.replace('.','_').replace('-','_')
    for path in destination.rglob('*'):
        if path.is_file():
            text=path.read_text(encoding='utf-8').replace('__ID__',identifier).replace('__NAME__',name).replace('__CRATE__',crate).replace('__TYPE__','UserMod').replace('__SDK__',ROOT.as_posix())
            if path.name=='Cargo.toml':text+='\n[workspace]\n'
            path.write_text(text,encoding='utf-8',newline='\n')
    print(destination)

def deploy_mod(folder):
    import tomllib
    cfg=config();folder=folder.resolve()
    if not folder.is_relative_to(ROOT/'mods'):raise ValueError('Deploy a local mods/ project')
    manifest=tomllib.loads((folder/'mod.toml').read_text(encoding='utf-8'))
    identifier=manifest['mod']['id'];dllname=manifest['mod']['dll']
    child(Path('root'),identifier);child(Path('root'),dllname)
    if '/' in identifier or '/' in dllname:raise ValueError('Mod ID/DLL must be single path components')
    run('cargo','build','--manifest-path',folder/'Cargo.toml','--release')
    built=folder/'target/release'/dllname
    if not built.is_file():raise ValueError(f'Built DLL not found: {built}')
    files={f'bbmods/{identifier}/{dllname}':built}
    if not (Path(cfg['runtime_directory'])/'bbmods'/identifier/'mod.toml').exists():
        files[f'bbmods/{identifier}/mod.toml']=folder/'mod.toml'
    deploy_files(Path(cfg['runtime_directory']),files)

def deploy_files(runtime, files):
    from datetime import datetime,timezone
    require_game_closed()
    backup=ROOT/'.local/backups'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    backup.mkdir(parents=True)
    records=[]
    # Preflight all destinations before touching an installed file.
    for rel,source in files.items():
        dest=child(runtime,rel)
        if not source.is_file() or source.stat().st_size==0:raise ValueError(f'Invalid deployment source: {source}')
        old=sha(dest) if dest.is_file() else None
        if dest.exists() and old is None:raise ValueError(f'Expected a file: {dest}')
        if old:
            saved=child(backup,rel);saved.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(dest,saved)
        records.append({'path':rel,'before':old,'after':sha(source)})
    save(backup/'manifest.json',{'runtime':str(runtime),'files':records})
    try:
        for record in records:
            source=files[record['path']];dest=child(runtime,record['path'])
            from decompile_lua import atomic
            atomic(dest,source.read_bytes())
    except Exception:
        rollback(backup,partial=True)
        raise
    print(f'Deployed {len(records)} files. Rollback: .\\bb.ps1 rollback "{backup}"')

def require_game_closed():
    # Refuse replacement while any game/launcher instance is running.
    if os.name=='nt':
        check=subprocess.check_output([powershell(),'-NoProfile','-Command',
            'Write-Output @(Get-Process -Name bb-probe,Bloodborne,"Play Bloodborne" -ErrorAction SilentlyContinue).Count'],text=True)
        if check.strip()!='0':raise ValueError('Close the game and launcher before deployment.')

def rollback(backup,partial=False):
    require_game_closed()
    backup=backup.resolve()
    if not backup.is_relative_to(ROOT/'.local/backups'):raise ValueError('Use a toolkit backup directory')
    data=json.loads((backup/'manifest.json').read_text());runtime=Path(data['runtime'])
    for record in data['files']:
        dest=child(runtime,record['path']);current=sha(dest) if dest.is_file() else None
        if current not in ((record['after'],record['before']) if partial else (record['after'],)):
            raise ValueError(f'Installed file changed since deployment; preserved: {dest}')
        saved=child(backup,record['path'])
        if record['before'] and sha(saved)!=record['before']:raise ValueError('Backup checksum mismatch')
    from decompile_lua import atomic
    for record in data['files']:
        dest=child(runtime,record['path'])
        if record['before']:atomic(dest,child(backup,record['path']).read_bytes())
        else:dest.unlink(missing_ok=True)
    print('Rollback complete; saves and settings were not involved.')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    commands=parser.add_subparsers(dest='command',required=True)
    setup=commands.add_parser('setup');setup.add_argument('--game',type=Path,required=True);setup.add_argument('--runtime',type=Path,required=True);setup.add_argument('--mode',choices=['full','scripts'],default='full');setup.add_argument('--configure-only',action='store_true');setup.add_argument('--tool-cache',type=Path)
    commands.add_parser('doctor')
    tools=commands.add_parser('install-tools');tools.add_argument('--tool-cache',type=Path)
    ref=commands.add_parser('extract');ref.add_argument('--mode',choices=['full','scripts'],default='full')
    annotate=commands.add_parser('annotate');annotate.add_argument('--placements',type=Path)
    new=commands.add_parser('new-mod');new.add_argument('id');new.add_argument('name')
    mod=commands.add_parser('deploy-mod');mod.add_argument('project',type=Path)
    roll=commands.add_parser('rollback');roll.add_argument('backup',type=Path)
    commands.add_parser('smithbox')
    commands.add_parser('build-runtime')
    commands.add_parser('deploy-runtime')
    build=commands.add_parser('build-mod',help='Build an independent mod project without deployment')
    build.add_argument('project',type=Path);build.add_argument('--test',action='store_true')
    content=commands.add_parser('content',help='Run the canonical content compiler')
    content.add_argument('arguments',nargs=argparse.REMAINDER)
    assets=commands.add_parser('native-assets',help='Generate private native-menu assets from the selected game')
    assets.add_argument('--game',type=Path);assets.add_argument('--out',type=Path)
    ui=commands.add_parser('ui-inspect',help='Inspect GFX structure into private read-only research reports')
    ui.add_argument('--game',type=Path);ui.add_argument('--out',type=Path)
    ui.add_argument('--movie',action='append')
    ui.add_argument('--decompile',action='store_true')
    links=commands.add_parser('script-links',help='Generate an observer project script-reference index')
    links.add_argument('--mod',type=Path,required=True);links.add_argument('--check',action='store_true')
    links.add_argument('--raw',type=Path);links.add_argument('--decompiled',type=Path)
    maps=commands.add_parser('map-placements',help='Export private character map placements')
    maps.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.command=='setup':
        cfg=validate(args.game,args.runtime);save(CONFIG,cfg)
        print(json.dumps(cfg,indent=2))
        if not args.configure_only:install_tools(args.tool_cache);references(args.mode)
    elif args.command=='doctor':
        cfg=config();print(json.dumps(cfg,indent=2))
        for name in ['python','git','dotnet','cargo']:print(name,shutil.which(name) or 'MISSING')
        print('Runtime marker is indicative only. No gameplay/capability validation has been performed.')
    elif args.command=='install-tools':install_tools(args.tool_cache)
    elif args.command=='extract':references(args.mode)
    elif args.command=='annotate':
        command=[sys.executable,Path(__file__).with_name('annotate_scripts.py')]
        if args.placements:command+=['--placements',args.placements]
        run(*command)
    elif args.command=='new-mod':new_mod(args.id,args.name)
    elif args.command=='deploy-mod':deploy_mod(args.project)
    elif args.command=='rollback':rollback(args.backup)
    elif args.command=='build-runtime':
        run(powershell(),'-NoProfile','-ExecutionPolicy','Bypass','-File',Path(__file__).with_name('build-runtime.ps1'))
    elif args.command=='build-mod':
        arguments=[powershell(),'-NoProfile','-ExecutionPolicy','Bypass','-File',
                   Path(__file__).with_name('build-mod.ps1'),'-Project',args.project.resolve()]
        if args.test:arguments.append('-Test')
        run(*arguments)
    elif args.command=='content':
        run(sys.executable,Path(__file__).with_name('content.py'),*args.arguments)
    elif args.command=='native-assets':
        arguments=[sys.executable,Path(__file__).with_name('native_menu_assets.py')]
        if args.game:arguments+=['--game',args.game.resolve()]
        if args.out:arguments+=['--out',args.out.resolve()]
        run(*arguments)
    elif args.command=='ui-inspect':
        arguments=[sys.executable,Path(__file__).with_name('inspect_ui.py')]
        if args.game:arguments+=['--game',args.game.resolve()]
        if args.out:arguments+=['--out',args.out.resolve()]
        for movie in args.movie or []:arguments+=['--movie',movie]
        if args.decompile:arguments.append('--decompile')
        run(*arguments)
    elif args.command=='script-links':
        arguments=[sys.executable,Path(__file__).with_name('generate_observer_script_links.py'),'--mod',args.mod.resolve()]
        if args.check:arguments.append('--check')
        if args.raw:arguments+=['--raw',args.raw.resolve()]
        if args.decompiled:arguments+=['--decompiled',args.decompiled.resolve()]
        run(*arguments)
    elif args.command=='map-placements':
        shell=shutil.which('pwsh')
        if not shell:raise ValueError('Map export requires PowerShell 7 (pwsh).')
        run(shell,'-NoProfile','-ExecutionPolicy','Bypass','-File',
            Path(__file__).with_name('export-map-script-placements.ps1'),'-Output',args.output.resolve())
    elif args.command=='deploy-runtime':
        cfg=config();stage=ROOT/'.local/runtime-stage'
        manifest=json.loads((stage/'manifest.json').read_text())
        files={rel:child(stage,rel) for rel in manifest['files']}
        for rel,path in files.items():
            if sha(path)!=manifest['files'][rel]:raise ValueError(f'Runtime build changed: {rel}')
        deploy_files(Path(cfg['runtime_directory']),files)
    elif args.command=='smithbox':
        cfg=config();folder=ROOT/'tools/Smithbox/2.2.6';info=json.loads((folder/'installed.json').read_text())
        print('Smithbox game root:',cfg['game_assets']);print('Choose a separate project folder under mods/.')
        subprocess.Popen([str(child(folder,info['executable']))],cwd=folder)

if __name__=='__main__':
    try:main()
    except (ValueError,OSError,subprocess.CalledProcessError) as error:
        print(f'ERROR: {error}',file=sys.stderr);raise SystemExit(1)
