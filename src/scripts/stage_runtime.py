"""Record the exact local runtime files eligible for separate deployment."""
import json
from toolkit import ROOT,LOCK,sha,save

stage=ROOT/'.local/runtime-stage'
paths=[p for p in (stage/'bin').iterdir() if p.suffix.lower() in ('.exe','.dll')]
if not any(p.name=='bb-probe.exe' for p in paths):raise SystemExit('Missing staged bb-probe.exe')
save(stage/'manifest.json',{'source':LOCK['runtime'],'files':{p.relative_to(stage).as_posix():sha(p) for p in sorted(paths)}})
