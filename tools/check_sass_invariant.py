#!/usr/bin/env python3
"""Kernel invariant: per-kernel SASS .text of the Blackwell native must equal tools/gate1a/final-sass.json (67 SM120).

usage: check_sass_invariant.py [--toolkit DIR] [--jobs N] [--keep DIR]
       check_sass_invariant.py --platform windows --binary canary-miner.exe [--toolkit DIR]
       (windows: digests the given MSVC binary; tools/gate1a/windows-name-map.json pairs the size_t-mangled names; nothing else is mapped)

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
ap.add_argument('--platform',choices=['linux','windows'],default='linux',help='windows: digest an existing --binary and apply tools/gate1a/windows-name-map.json only')
ap.add_argument('--binary',default=None,help='windows: the MSVC-built canary-miner.exe to digest')
a=ap.parse_args()
EXE='.exe' if a.platform=='windows' else ''
if not a.toolkit or not (pathlib.Path(a.toolkit)/'bin'/('nvcc'+EXE)).exists():
    print('check_sass_invariant: FAILED - set CANARY_TOOLKIT (or --toolkit) to a CUDA 13.1 toolkit root; this stage is mandatory and never skipped')
    sys.exit(2)
tk=pathlib.Path(a.toolkit)
final=json.load(open(HERE/'gate1a'/'final-sass.json'))
if a.platform=='windows':
    # Explicit pairing only: each mapped Windows name must be a Linux kernel with identical bytes; every other name must match.
    if not a.binary: print('check_sass_invariant: FAILED - --platform windows needs --binary'); sys.exit(2)
    import importlib.util
    spec=importlib.util.spec_from_file_location('sass_digest',HERE/'sass_digest.py'); sd=importlib.util.module_from_spec(spec); spec.loader.exec_module(sd)
    want=final['sm120']['recipe']; got=sd.digests(a.binary,str(tk/'bin'/('cuobjdump'+EXE)))
    mapping=json.load(open(HERE/'gate1a'/'windows-name-map.json'))
    for lk,wk in mapping.items():
        if lk not in want or wk not in got or lk in got or want[lk]!=got[wk]:
            print('check_sass_invariant: FAILED - windows-name-map entry does not pair two identical kernels:',lk); sys.exit(1)
        got[lk]=got.pop(wk)
    diff=[k for k in sorted(set(want)|set(got)) if want.get(k)!=got.get(k)]
    print(f'windows: final-sass {len(want)} kernels, built {len(got)} kernels (after {len(mapping)} mapped names),',('EQUAL' if not diff else f'DIFFERENT ({len(diff)}): {diff[:4]}'))
    if diff: sys.exit(1)
    print('check_sass_invariant: OK (%d/%d windows SM120 per-kernel SASS equal, %d mapped names)' % (len(want),len(want),len(mapping)))
    sys.exit(0)
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
