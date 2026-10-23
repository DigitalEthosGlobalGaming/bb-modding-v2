"""Add support for another bbport release.

    python src/scripts/add_runtime.py windows-v1.9.5 [--patch src/integration/windows-v1.9.4.patch] [--dry-run]
    python src/scripts/add_runtime.py --latest [--dry-run]        # the newest upstream release not yet supported

Looks up the release's commit, checks whether an integration patch applies to it (the newest
supported runtime's by default), reads the release's stock bb-probe.exe hash from its download,
and adds the entry to tools.lock.json. If the patch does not apply, rebase it first: apply with
`git apply --reject` in a checkout of the tag, fix the rejected hunks, export the result with
`git diff --cached --binary`, and pass it with --patch.
Then build it (`.\\bb.ps1 build-runtime -Runtime <id>`) and rebuild the installer.

Exit status (for automation): 0 added (or, with --dry-run, would apply), 10 nothing new to add,
3 the patch does not apply and needs a human to rebase it.
"""
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

from toolkit import ROOT, runtime_spec

LOCK_PATH = Path(__file__).with_name('tools.lock.json')
API = 'https://api.github.com/repos/Supermedo/bloodborne_pc'
NOTHING_NEW, NEEDS_REBASE = 10, 3


def fetch_json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'bb-modding'})) as r:
        return json.load(r)


def stock_probe_sha(release):
    asset = next(a for a in release['assets'] if a['name'].endswith('Windows.zip'))
    with tempfile.TemporaryDirectory() as tmp:
        archive = Path(tmp) / 'release.zip'
        print(f"Downloading {asset['name']} ({asset['size'] // 1048576} MiB) to read bb-probe.exe...")
        urllib.request.urlretrieve(asset['browser_download_url'], archive)
        with zipfile.ZipFile(archive) as z:
            name = next(n for n in z.namelist() if n.endswith('bin/bb-probe.exe') or n == 'bb-probe.exe')
            return hashlib.sha256(z.read(name)).hexdigest()


def newest_unsupported(known):
    """The most recently published windows-v* release that tools.lock.json does not list."""
    releases = [r for r in fetch_json(f'{API}/releases?per_page=30') if r['tag_name'].startswith('windows-v') and not r['draft'] and not r['prerelease']]
    releases.sort(key=lambda r: r['published_at'], reverse=True)
    return next((r for r in releases if r['tag_name'] not in known), None)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('tag', nargs='?', help='bbport release tag, e.g. windows-v1.9.5')
    parser.add_argument('--latest', action='store_true', help='use the newest release that is not supported yet')
    parser.add_argument('--patch', help="integration patch to use (default: the newest supported runtime's)")
    parser.add_argument('--dry-run', action='store_true', help='check only; do not download or change tools.lock.json')
    args = parser.parse_args()
    if bool(args.tag) == args.latest:
        parser.error('give a release tag or --latest')

    lock = json.loads(LOCK_PATH.read_text())
    known = {r['id'] for r in lock['runtimes']}
    base = runtime_spec()
    patch = args.patch or base['integration']
    if args.latest:
        release = newest_unsupported(known)
        if not release:
            print('Every upstream release is already supported.')
            raise SystemExit(NOTHING_NEW)
        tag = release['tag_name']
    else:
        tag = args.tag
        release = fetch_json(f'{API}/releases/tags/{tag}')
        if tag in known:
            print(f'{tag} is already supported.')
            raise SystemExit(NOTHING_NEW)
    commit = fetch_json(f'{API}/commits/{tag}')['sha']

    checkout = ROOT / base['dir']
    subprocess.run(['git', '-C', str(checkout), 'fetch', '--tags', 'origin'], check=True)
    work = ROOT / '.local/wt-check'
    subprocess.run(['git', '-C', str(checkout), '-c', 'core.longpaths=true', 'worktree', 'add', '-f', '--detach', str(work), commit], check=True)
    try:
        applied = subprocess.run(['git', '-C', str(work), 'apply', '--check', str(ROOT / patch)], capture_output=True, text=True)
    finally:
        subprocess.run(['git', '-C', str(checkout), 'worktree', 'remove', '--force', str(work)], check=False)
    if applied.returncode:
        print(f'{patch} does not apply to {tag} ({commit[:10]}):\n{applied.stderr.strip()}')
        print("Rebase the patch for this release (see this script's docstring), then pass it with --patch.")
        raise SystemExit(NEEDS_REBASE)
    print(f'{patch} applies to {tag} ({commit[:10]}).')
    if args.dry_run:
        return
    version = tag.split('-v')[-1]
    entry = {
        'id': tag, 'label': f'bbport {version}', 'repository': base['repository'], 'commit': commit,
        'dir': f'tools/rt-{version}', 'integration': patch,
        'stock_probe_sha256': [stock_probe_sha(release)], 'capabilities': base['capabilities'],
    }
    lock['runtimes'].insert(0, entry)
    lock['default_runtime'] = entry['id']
    LOCK_PATH.write_text(json.dumps(lock, indent=2) + '\n')
    print(f"Added {entry['id']} (default). Next: .\\bb.ps1 build-runtime -Runtime {entry['id']}, then src/installer/build.ps1.")


if __name__ == '__main__':
    sys.exit(main())
