#!/usr/bin/env python3
"""Print exact local source revisions; does not build, fetch, deploy or change refs."""
import argparse
import json
import subprocess
from pathlib import Path

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('core','playerbots','dungeon-clear','autonomy'):
        parser.add_argument('--'+name,required=True,type=Path)
    args=parser.parse_args()
    manifest={}
    for name,path in vars(args).items():
        def git(*cmd):
            return subprocess.check_output(['git','-C',str(path),*cmd],text=True).strip()
        dirty=git('status','--porcelain')
        if dirty:
            raise SystemExit(f'{name}: dirty checkout; pin a committed source set before the manual build')
        manifest[name]={'sha':git('rev-parse','HEAD'),'branch':git('branch','--show-current'),'origin':git('remote','get-url','origin')}
    print(json.dumps(manifest,indent=2))

if __name__=='__main__':
    main()
