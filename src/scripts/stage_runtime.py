"""Record the exact local runtime files eligible for separate deployment."""
import argparse
import json
from toolkit import ROOT, sha, save, runtime_spec

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--runtime-id', help='Supported bbport version (default: default_runtime in tools.lock.json)')
spec = runtime_spec(parser.parse_args().runtime_id)
stage = ROOT / '.local/runtime-stage' / spec['id']
paths = [p for p in (stage / 'bin').iterdir() if p.suffix.lower() in ('.exe', '.dll')]
if not any(p.name == 'bb-probe.exe' for p in paths):
    raise SystemExit('Missing staged bb-probe.exe')
save(stage / 'manifest.json', {'source': spec, 'files': {p.relative_to(stage).as_posix(): sha(p) for p in sorted(paths)}})
