"""Materialise the reviewed framework in a pinned, isolated port checkout."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
from toolkit import ROOT, checkout, sha, save, runtime_spec


def git(target, *args):
    return subprocess.check_output(['git', '-C', str(target), *args], text=True).strip()


def prepare(target=None, runtime_id=None):
    spec = runtime_spec(runtime_id)
    target = Path(target).resolve() if target else ROOT / spec['dir']
    patch = ROOT / spec['integration']
    stamp = target / '.bbmod-source.json'
    if stamp.exists():
        state = json.loads(stamp.read_text())
        if state['patch_sha256'] != sha(patch) or state['commit'] != spec['commit']:
            raise ValueError(f'Runtime integration changed; preserve this checkout and prepare a fresh one: {target}')
        if git(target, 'rev-parse', 'HEAD') != spec['commit']:
            raise ValueError(f'Runtime HEAD changed; source preserved: {target}')
        # Reverse-check proves the complete integration is still present. Do not
        # silently reuse an incomplete/edited bridge with a valid old stamp.
        subprocess.run(['git', '-C', str(target), 'apply', '--reverse', '--check', str(patch)], check=True)
    else:
        checkout(spec['repository'], spec['commit'], target)
        subprocess.run(['git', '-C', str(target), 'apply', '--check', str(patch)], check=True)
        subprocess.run(['git', '-C', str(target), 'apply', str(patch)], check=True)
        state = {'commit': spec['commit'], 'patch_sha256': sha(patch), 'framework': {}}
    framework = target / 'third_party/bbmod'
    previous = state.get('framework', {})
    # Only files materialised by us may be replaced or retired; preserve edits.
    for rel, digest in previous.items():
        dest = framework / rel
        if dest.exists() and sha(dest) != digest:
            raise ValueError(f'Materialised framework was edited; preserve it before refreshing: {dest}')
    current = {}
    for source, prefix in [(ROOT / 'src/host', 'host'), (ROOT / 'sdk', 'sdk')]:
        for path in source.rglob('*'):
            relative = path.relative_to(source)
            if not path.is_file() or any(x in ('local-data', '__pycache__', 'target') for x in relative.parts):
                continue
            rel = (Path(prefix) / relative).as_posix()
            dest = framework / rel
            digest = sha(path)
            if dest.exists() and rel not in previous and sha(dest) != digest:
                raise ValueError(f'Unowned framework file would be overwritten: {dest}')
            dest.parent.mkdir(parents=True, exist_ok=True)
            if not dest.exists() or sha(dest) != digest:
                shutil.copy2(path, dest)
            current[rel] = digest
    for rel in previous.keys() - current.keys():
        (framework / rel).unlink(missing_ok=True)
    state['framework'] = current
    save(stamp, state)
    save(ROOT / '.local' / f"runtime-source-{spec['id']}.json", {k: state[k] for k in ('commit', 'patch_sha256')})
    print(target)
    return target


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', type=Path, help='Fresh separate checkout; default is the runtime\'s dir in tools.lock.json')
    parser.add_argument('--runtime-id', help='Supported bbport version (default: default_runtime in tools.lock.json)')
    args = parser.parse_args()
    prepare(args.target, args.runtime_id)
