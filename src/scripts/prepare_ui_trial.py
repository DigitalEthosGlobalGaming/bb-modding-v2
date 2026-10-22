"""Prepare an isolated UI trial with copied saves/config and a verified runtime.

Preparation never launches or deploys. Run the generated launch.ps1 to play the
trial; its caches, logs and settings all live inside the new private directory.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil

from toolkit import ROOT, child, sha, save
import native_menu_assets


def verify_package(folder, manifest_name, key='files'):
    record = json.loads((folder/manifest_name).read_text(encoding='utf-8-sig'))
    files = record[key]
    if not files:
        raise ValueError(f'Empty package manifest: {folder}')
    for name, digest in files.items():
        if sha(child(folder, name)) != digest:
            raise ValueError(f'File changed since package validation: {name}')
    return record


def extra_mod_files(source):
    """(mod id, manifest bytes, dll name, dll path) of a built Rust mod project folder."""
    manifest = (source/'mod.toml').read_text(encoding='utf-8')
    fields = {}
    for line in manifest.splitlines():
        key, _, value = line.partition('=')
        fields.setdefault(key.strip(), value.strip().strip('"'))
    dll = source/'target/release'/fields['dll']
    if not dll.is_file():
        raise ValueError(f'Build {source.name} first: missing {dll}')
    return fields['id'], manifest, fields['dll'], dll


def prepare(config, stage, package, out, *, trace=False, components=False, extra_mods=()):
    runtime = Path(config['runtime_directory']).resolve()
    game = Path(config['game_assets']).resolve().parent
    out = out.resolve()
    for protected in (runtime, game, stage.resolve(), package.resolve()):
        if out == protected or protected in out.parents or out in protected.parents:
            raise ValueError('Trial must be separate from game, installation and source packages')
    if out.exists():
        raise ValueError('Choose a new trial directory; existing trials are preserved')
    runtime_record = verify_package(stage, 'manifest.json')
    package_record = verify_package(package, 'build-record.json')
    if package_record.get('mod_id') != 'example.ui-examples' or not package_record.get('automated_tests_passed'):
        raise ValueError('Build and test UI-Examples-Mod before preparing a trial')
    for p in [runtime/'run.py', runtime/'scripts', runtime/'bbport.ini', game/'eboot.bin']:
        if not p.exists():
            raise ValueError(f'Missing trial input: {p}')
    originals = {p: sha(p) for p in (runtime/'bbport.ini', runtime/'input.ini') if p.is_file()}
    saves = runtime/'user/savedata'
    if saves.exists():
        originals.update({p: sha(p) for p in saves.rglob('*') if p.is_file()})
    out.mkdir(parents=True)
    # run.py derives helper paths from its own location. Copy it so preparation,
    # restarts and all generated layers stay inside this isolated trial.
    shutil.copy2(runtime/'run.py', out/'run.py')
    shutil.copytree(runtime/'scripts', out/'scripts', ignore=shutil.ignore_patterns('__pycache__'))
    shutil.copytree(runtime/'bin', out/'bin')
    for relative in runtime_record['files']:
        destination = child(out, relative)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(child(stage, relative), destination)
    for name in ('bbport.ini', 'input.ini'):
        if (runtime/name).exists():
            shutil.copy2(runtime/name, out/name)
    # The installed debug-menu option can depend on font-only content mods.
    # This focused trial loads only UI-Examples-Mod, so disable that unrelated
    # debug menu in the copy rather than importing the installed mod set.
    ini = out/'bbport.ini'
    lines = ini.read_text(encoding='utf-8-sig').splitlines()
    lines = [line for line in lines if line.partition('=')[0].strip() != 'debug_menu']
    ini.write_text('\n'.join(lines+['debug_menu=0'])+'\n', encoding='utf-8')
    for name in ('patches', 'fsr4_shaders', 'fsr4_411'):
        if (runtime/name).is_dir():
            shutil.copytree(runtime/name, out/name)
    (out/'user').mkdir()
    if saves.exists():
        shutil.copytree(saves, out/'user/savedata')
    mods = out/'bbmods/example.ui-examples'
    mods.mkdir(parents=True)
    for name in package_record['files']:
        destination = child(mods, name)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(child(package, name), destination)
    for source in extra_mods:
        mod_id, manifest, dll_name, dll = extra_mod_files(Path(source))
        target = out/'bbmods'/mod_id
        target.mkdir(parents=True)
        (target/'mod.toml').write_text(manifest, encoding='utf-8')
        shutil.copy2(dll, target/dll_name)
    assets = out/'native-ui/dvdroot_ps4/menu'
    assets.mkdir(parents=True)
    menu = game/'dvdroot_ps4/menu'
    (assets/'ingametop.gfx').write_bytes(native_menu_assets.build((menu/'ingametop.gfx').read_bytes()))
    shutil.copy2(menu/'optionsetting.gfx', assets/'optionsetting.gfx')
    env = dict(BB_GAME_DIR=str(game), BB_DATA_DIR=str(out), BB_USER_DIR=str(out/'user'),
               BB_GPU_USER_DIR=str(out/'user'), BB_CONFIG=str(out/'bbport.ini'),
               BB_PROBE=str(out/'bin/bb-probe.exe'), BB_CODEMODS_DIR=str(out/'bbmods'),
               BB_MODS_LOG=str(out/'bbmods.log'), BB_MODS_DEV='1', BB_MODHOST='1',
               BB_MODS_SAFE='0', BB_MODS_NATIVE_UI='1', BB_MODS_NATIVE_UI_LIVE='1',
               BB_NATIVE_UI_ASSETS=str(out/'native-ui'), BB_ONLINE='0', BB_FPS='60')
    env['BB_MODS_NATIVE_UI_TRACE'] = '1' if trace else '0'
    env['BB_MODS_NATIVE_UI_COMPONENT_TRACE'] = '1' if components else '0'
    # Encode data as JSON, not interpolated shell command text.
    save(out/'environment.json', env)
    launcher = '''$ErrorActionPreference = 'Stop'
if (@(Get-Process -Name bb-probe,Bloodborne,'Play Bloodborne' -ErrorAction SilentlyContinue).Count) {
    throw 'Close the existing game and launcher before starting an isolated UI trial.'
}
$trialEnvironment = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'environment.json') -Raw | ConvertFrom-Json
foreach ($property in $trialEnvironment.PSObject.Properties) {
    [Environment]::SetEnvironmentVariable($property.Name, [string]$property.Value, 'Process')
}
@{ launcher_pid = $PID; started_utc = [DateTime]::UtcNow.ToString('o'); native_rendering_verified = $false } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'launch-status.json') -Encoding utf8
Push-Location $PSScriptRoot
try {
    & python -u (Join-Path $PSScriptRoot 'run.py') 1> (Join-Path $PSScriptRoot 'stdout.log') 2> (Join-Path $PSScriptRoot 'stderr.log')
    if ($LASTEXITCODE) { throw "Isolated runtime exited with code $LASTEXITCODE" }
} finally { Pop-Location }
'''
    (out/'launch.ps1').write_text(launcher, encoding='utf-8')
    for source, expected in originals.items():
        if sha(source) != expected:
            raise ValueError(f'Original settings/save changed during preparation: {source}')
    files = {p.relative_to(out).as_posix(): sha(p) for p in out.rglob('*') if p.is_file()}
    save(out/'trial.json', dict(schema_version=1, prepared_utc=datetime.now(timezone.utc).isoformat(),
                               mod_id='example.ui-examples', launched=False,
                               native_rendering_verified=False,
                               native_ui_trace=trace,
                               native_ui_component_trace=components,
                               trial_setting_overrides={'debug_menu': '0'},
                               original_hashes={str(p): h for p, h in originals.items()}, files=files))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--trace', action='store_true', help='Record bounded native slot state diagnostics')
    parser.add_argument('--components', action='store_true', help='Observe native movie requests, Steps and selection; no additional hooks')
    parser.add_argument('--extra-mod', action='append', type=Path, default=[], help='Built Rust mod project folder to include (repeatable)')
    args = parser.parse_args()
    private = ROOT/'.local/ui-trials'
    out = args.out or private/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    if private.resolve() not in out.resolve().parents:
        raise ValueError('Trial output must be a new child of .local/ui-trials')
    cfg = json.loads((ROOT/'.local/workspace.json').read_text())
    result = prepare(cfg, ROOT/'.local/runtime-stage', ROOT/'dist/example.ui-examples', out,
                     trace=args.trace, components=args.components, extra_mods=[ROOT/m if not m.is_absolute() else m for m in args.extra_mod])
    print(f'Prepared isolated UI trial: {result}')
    print('Run its launch.ps1; original installation, configuration and saves were preserved.')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError) as error:
        raise SystemExit(f'ERROR: {error}')
