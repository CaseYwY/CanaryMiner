#!/usr/bin/env python3
"""Kernel invariant: per-kernel SASS .text of the Blackwell native must equal tools/gate1a/final-sass.json (67 SM120).

usage: check_sass_invariant.py [--toolkit DIR] [--jobs N] [--keep DIR]

Builds the recipe native (tools/build_natives.py) for SM120 from this tree, digests every kernel
(tools/sass_digest.py) and compares with the 'recipe' section of final-sass.json. Mandatory stage of tools/test_all.sh:
the toolkit (CUDA 13.1 root with bin/nvcc and bin/cuobjdump) comes from --toolkit or CANARY_TOOLKIT; if neither is set
the check FAILS (exit 2) rather than skipping. Exit 1 on any kernel difference, 0 when both architectures are identical.
"""
import argparse,json,os,pathlib,subprocess,sys,tempfile
HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parent
ap=argparse.ArgumentParser()
ap.add_argument('--toolkit',default=os.environ.get('CANARY_TOOLKIT'))
ap.add_argument('--jobs',type=int,default=4)
ap.add_argument('--keep',default=None,help='keep the build output in this directory')
a=ap.parse_args()
if not a.toolkit or not (pathlib.Path(a.toolkit)/'bin'/'nvcc').exists():
    print('check_sass_invariant: FAILED - set CANARY_TOOLKIT (or --toolkit) to a CUDA 13.1 toolkit root; this stage is mandatory and never skipped')
    sys.exit(2)
tk=pathlib.Path(a.toolkit)
final=json.load(open(HERE/'gate1a'/'final-sass.json'))
rc=0
work=pathlib.Path(a.keep) if a.keep else pathlib.Path(tempfile.mkdtemp(prefix='canary-sass-'))
work.mkdir(parents=True,exist_ok=True)
for arch in ('120',):
    out=work/('sm'+arch)
    r=subprocess.run([sys.executable,str(HERE/'build_natives.py'),arch,'--toolkit',str(tk),'--src',str(ROOT),'--out',str(out),'--jobs',str(a.jobs)],capture_output=True,text=True)
    if r.returncode!=0:
        print(f'sm{arch}: BUILD FAILED rc={r.returncode}'); print(r.stdout[-1500:]); print(r.stderr[-1500:]); rc=1; continue
    dj=work/f'sm{arch}.json'
    r=subprocess.run([sys.executable,str(HERE/'sass_digest.py'),str(out/'canary-miner'),str(dj),'--cuobjdump',str(tk/'bin'/'cuobjdump')],capture_output=True,text=True)
    if r.returncode!=0:
        print(f'sm{arch}: DIGEST FAILED'); print(r.stderr[-800:]); rc=1; continue
    want=final['sm'+arch]['recipe']; got=json.load(open(dj))
    diff=[k for k in sorted(set(want)|set(got)) if want.get(k)!=got.get(k)]
    print(f'sm{arch}: final-sass {len(want)} kernels, built {len(got)} kernels,',('EQUAL' if not diff else f'DIFFERENT ({len(diff)}): {diff[:4]}'))
    if diff: rc=1
    else: equal=len(want)
if rc==0: print('check_sass_invariant: OK (%d/%d SM120 per-kernel SASS equal)' % (equal, equal))
sys.exit(rc)
