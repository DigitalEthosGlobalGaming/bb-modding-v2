"""Extract and compile Bloodborne content mods without changing the base game."""
import argparse
import contextlib
import json
import os
from pathlib import Path
import shutil
import tempfile

import content_pipeline as c


def write_json(path, value):
    data = json.dumps(value, indent=2, ensure_ascii=False) + '\n'
    atomic_write(path, data.encode('utf-8'))


def atomic_write(path, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as f:
        f.write(payload)
        temporary = Path(f.name)
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


@contextlib.contextmanager
def workspace_lock(workspace):
    workspace.mkdir(parents=True, exist_ok=True)
    path = workspace / '.build-lock'
    try:
        fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        raise SystemExit(f'Another content command is running (or stale lock): {path}')
    try:
        os.write(fd, str(os.getpid()).encode())
        yield
    finally:
        os.close(fd)
        path.unlink()


def extract(game, workspace):
    """Extract nested archives needed for item development, not another game copy."""
    outputs, source_hashes = {}, {}
    archives = [c.PARAM_PATH, 'msg/engus/item.msgbnd.dcx']
    for rel in archives:
        raw = (game / rel).read_bytes()
        source_hashes[rel] = c.digest(raw)
        bnd, entries = c.read_parameters(raw)
        prefix = 'params' if rel == c.PARAM_PATH else 'text/engus'
        # Use binder IDs for text filenames: avoids localized/unsafe names.
        for name, (file_id, data) in entries.items():
            filename = name if prefix == 'params' else f'{file_id}.fmg'
            c.safe_relative(filename)
            outputs[f'{prefix}/{filename}'] = data
            if prefix != 'params':
                outputs[f'{prefix}/{file_id}.json'] = (json.dumps(c.read_fmg(data), ensure_ascii=False, indent=2)+'\n').encode('utf-8')
    schema = c.fields_schema()
    report = dict(schema_version=1, source_hashes=source_hashes, files={p:c.digest(b) for p,b in sorted(outputs.items())})
    target = workspace / 'extracted'
    for rel, payload in outputs.items():
        atomic_write(c.checked_child(target, rel), payload)
    write_json(target / 'field-schema.json', schema)
    write_json(target / 'extraction-report.json', report)
    print(f'Extracted {len(outputs)} parameter/text files to {target}')
    print('Existing game extraction remains the asset source; no game files were modified.')


def cache_key(game, specs, lock):
    inputs = {str(Path(__file__).resolve()):c.digest(Path(__file__).read_bytes()),
              str(Path(c.__file__).resolve()):c.digest(Path(c.__file__).read_bytes())}
    files = [game / c.PARAM_PATH, c.FRAMEWORK / 'sdk/rust/bbmod/local-data/items.json',
             c.ROOT / 'src/scripts/param_schema.py',
             c.ROOT / 'src/scripts/archive_formats.py']
    files += sorted((c.ROOT / 'tools/Smithbox/2.2.6/Assets/PARAM/BB/Defs').glob('*.xml'))
    files += sorted((game / 'msg').glob('*/item.msgbnd.dcx'))
    for _, path, spec in specs:
        files.append(path)
        files += [c.checked_child(path.parent, a['source']) for a in spec.get('assets', [])]
        for asset in spec.get('assets', []):
            if c.checked_child(game, asset['target']).exists():
                raise ValueError('Asset target now conflicts with base game: ' + asset['target'])
    for path in files:
        inputs[str(path.resolve())] = c.digest(path.read_bytes())
    return c.digest(json.dumps(dict(inputs=inputs, lock=lock), sort_keys=True).encode()), inputs


def verify_build(path):
    manifest = json.loads((path / 'build-files.json').read_text(encoding='utf-8'))
    for rel, expected in manifest.items():
        target = c.checked_child(path, rel)
        if c.digest(target.read_bytes()) != expected:
            raise ValueError(f'Cached output changed: {target}')


def build(game, workspace, paths):
    specs = c.read_specs(paths)
    lock_path = workspace / 'item-ids.lock.json'
    lock = json.loads(lock_path.read_text(encoding='utf-8')) if lock_path.exists() else dict(schema_version=1, items={})
    c.validate_lock(lock)
    key, inputs = cache_key(game, specs, lock)
    current_path = workspace / 'current.json'
    if current_path.exists():
        current = json.loads(current_path.read_text(encoding='utf-8'))
        old_build = Path(current['build']).resolve()
        if not old_build.is_relative_to((workspace / 'builds').resolve()):
            raise ValueError('Cached build path escapes workspace')
        if current.get('cache_key') == key:
            verify_build(old_build)
            print(f'Unchanged content; using cached build: {old_build}')
            return
    output, registry, report = c.compile_content(game, specs, lock)
    key, inputs = cache_key(game, specs, lock)  # include committed assignments
    report['build_key'] = key
    folder = workspace / 'builds' / key[:20]
    if folder.exists():
        verify_build(folder)
    else:
        folder.parent.mkdir(parents=True, exist_ok=True)
        staging = Path(tempfile.mkdtemp(prefix='.content-stage-', dir=folder.parent))
        try:
            files = {'mod/dvdroot_ps4/'+rel:body for rel,body in output.items()}
            files['content-registry.json'] = (json.dumps(registry, ensure_ascii=False, indent=2)+'\n').encode('utf-8')
            files['content_registry.rs'] = c.rust_registry(registry).encode('utf-8')
            files['report.json'] = (json.dumps(report, ensure_ascii=False, indent=2)+'\n').encode('utf-8')
            files['inputs.json'] = (json.dumps(inputs, indent=2)+'\n').encode('utf-8')
            files['mod/mod.toml'] = b'[mod]\nid = "bbmod.compiled_content"\nname = "Compiled item content"\nversion = "0.1.0"\nabi = "1.4"\n[load]\nhot_reload = false\n'
            files['mod/CONTENT-STATUS.txt'] = (f'Build: {key}\nRestart required. Experimental new rows/assets: gameplay validation pending.\nDo not remove content from a save that owns its items without a migration.\n').encode()
            for rel, payload in files.items():
                path = c.checked_child(staging, rel)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(payload)
            write_json(staging / 'build-files.json', {p:c.digest(b) for p,b in sorted(files.items())})
            verify_build(staging)
            staging.rename(folder)
        finally:
            if staging.exists():
                resolved = staging.resolve()
                if resolved.parent != (workspace/'builds').resolve() or not resolved.name.startswith('.content-stage-'):
                    raise ValueError('Refusing cleanup outside build staging directory')
                shutil.rmtree(resolved)
    write_json(lock_path, lock)
    write_json(current_path, dict(schema_version=1, build=str(folder.resolve()), cache_key=key))
    print(f'Built {len(report["new_items"])} new item rows, {len(report["field_changes"])} field edits, {len(output)} overlay files')
    print(f'Build: {folder}')
    print('No runtime/game installation was changed. IDs are retained in item-ids.lock.json.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['extract', 'build', 'verify'])
    parser.add_argument('--game', type=Path, default=c.GAME)
    parser.add_argument('--workspace', type=Path, default=c.ROOT / '.local/content-workspace')
    parser.add_argument('--manifest', type=Path, action='append', default=[])
    args = parser.parse_args()
    game, workspace = args.game.resolve(), args.workspace.resolve()
    if workspace == game or workspace.is_relative_to(game) or game.is_relative_to(workspace):
        parser.error('Content workspace must be separate from the game source tree')
    with workspace_lock(workspace):
        if args.command == 'extract':
            extract(game, workspace)
        elif args.command == 'build':
            if not args.manifest:
                parser.error('build requires at least one --manifest')
            build(game, workspace, args.manifest)
        else:
            current = json.loads((workspace/'current.json').read_text(encoding='utf-8'))
            target = Path(current['build']).resolve()
            if not target.is_relative_to((workspace/'builds').resolve()):
                parser.error('Build path escapes workspace')
            verify_build(target)
            print(f'All generated file hashes verified: {target}')


if __name__ == '__main__':
    main()
