"""Materialise the reviewed framework in a pinned port checkout's expected layout."""
import argparse
from pathlib import Path
import shutil
import subprocess
from toolkit import ROOT,LOCK,checkout,sha,save

def prepare():
    target=ROOT/'tools/bloodborne_pc';spec=LOCK['runtime']
    # The initial preparation must start clean; subsequent builds reuse the stamped checkout.
    stamp=ROOT/'.local/runtime-source.json'
    if stamp.exists():
        state=__import__('json').loads(stamp.read_text())
        if state['patch_sha256']!=sha(ROOT/spec['integration']):raise ValueError('Integration patch changed; preserve this checkout and prepare a fresh one.')
        head=subprocess.check_output(['git','-C',str(target),'rev-parse','HEAD'],text=True).strip()
        if state['commit']!=spec['commit'] or head!=spec['commit']:raise ValueError('Runtime source pin changed; preserve it and prepare a fresh checkout.')
    else:
        checkout(spec['repository'],spec['commit'],target)
        patch=ROOT/spec['integration']
        subprocess.run(['git','-C',str(target),'apply','--check',str(patch)],check=True)
        subprocess.run(['git','-C',str(target),'apply',str(patch)],check=True)
        save(stamp,{'commit':spec['commit'],'patch_sha256':sha(patch)})
    framework=target/'third_party/bbmod';framework.mkdir(parents=True,exist_ok=True)
    for source,dest in [(ROOT/'src/host',framework/'host'),(ROOT/'sdk',framework/'sdk')]:
        shutil.copytree(source,dest,dirs_exist_ok=True,ignore=shutil.ignore_patterns('local-data','__pycache__'))
    # Local profiles can accompany a local build, but are never published.
    print(target)

if __name__=='__main__':prepare()
