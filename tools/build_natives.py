#!/usr/bin/env python3
"""Compile-only Pearl native build, with separate host-only display objects (no GPU is used).

Kernel flags retain the qualified v18 (SM120) recipe, parameterised by toolkit path.
The added host display objects do not constitute a newly qualified production binary.
usage:  python3 tools/build_natives.py 120 --toolkit DIR --out DIR [--src DIR] [--only]

  --toolkit DIR   a CUDA 13.1 toolkit root containing bin/nvcc (the qualified builds used nvcc 13.1.115)
  --only          compile only the kernel translation unit (algo.cu); skips the link
Then compare kernels:   python3 tools/sass_digest.py OUT/canary-miner out.json --cuobjdump DIR/bin/cuobjdump
"""
import argparse,os,sys,subprocess,hashlib,pathlib,concurrent.futures as cf
ap=argparse.ArgumentParser()
ap.add_argument('arch',choices=['120']); ap.add_argument('--toolkit',required=True); ap.add_argument('--out',required=True)
ap.add_argument('--src',default=str(pathlib.Path(__file__).resolve().parent.parent)); ap.add_argument('--only',action='store_true')
ap.add_argument('--compat-include',default=None,help='optional patched CUDA include dir (not needed on Ubuntu 24.04)')
ap.add_argument('--jobs',type=int,default=4)
a=ap.parse_args()
src=pathlib.Path(a.src).resolve(); out=pathlib.Path(a.out).resolve(); T=pathlib.Path(a.toolkit).resolve(); NVCC=str(T/'bin/nvcc')
PEARL=['EXPERIMENT_COMMON_DIM=8192','EXPERIMENT_LARGE_SHAPES=1','EXPERIMENT_PINNED_TIMING_ONLY=1','COMPRESSIBLE_MATRICES=1',
       'ENABLE_SHAPE_EXPERIMENT=1','ASYNC_SUBMIT_EXPERIMENT=1','ONLY_BUILD=1','GUARD_RETIREMENT_EXPERIMENT=1',
       'ENABLE_PRIVATE_GENERIC_LOOKAHEAD=1','ENABLE_ATTEMPT_OVERLAP=1','ENABLE_PUBLIC_GENERIC_LOOKAHEAD=1','MIXED_SCHEDULE_PROBE=1']
if a.arch=='120':
    defs=['CANARY_PEARL_'+d for d in PEARL+['ENABLE_SM120_NATIVE_EVIDENCE=1','ENABLE_TMA_EARLY_FUSION=1']]
    flags=['-O3','-arch=sm_120a','-Xcompiler=-Wall,-Wformat,-Werror=format']; seeds=['2090ed724510a4ddf18dc3cd6bb5a58c7c9f6c5f967583fb499618d5146e58bf']*5; extra=[]
    inc=['-I'+str(src/'src/core'),'-I'+str(src/'src')]
if a.compat_include: inc=['-I'+a.compat_include]+inc
TUS=[('miner','src/core/miner.cu'),('registry','src/core/registry.cu'),('pearl_algo','src/algos/pearl-pow/algo.cu'),('run','src/core/run.cpp')]
idx={n:i for i,(n,_) in enumerate(TUS)}
if a.only: TUS=[t for t in TUS if t[0]=='pearl_algo']
else: TUS += [('exact_decimal','src/product/exact_decimal.cpp'),('manual_market','src/product/manual_market.cpp')]
out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,PATH=str(T/'bin')+os.pathsep+os.environ['PATH'])
def comp(t):
    name,rel=t; o=out/(name+'.o')
    if name in idx:
        argv=[NVCC,'-c','-x','cu',*flags,'--std=c++17','-lineinfo','-ccbin=/usr/bin/g++',*inc,*extra,
              *['-D'+d for d in defs],'--frandom-seed='+seeds[idx[name]],str(src/rel),'-o',str(o)]
    else:
        # Pure host arithmetic/projector; no CUDA preprocessing or optional HTTPS backend is needed.
        argv=['/usr/bin/g++','-c','-O3','-std=c++17','-Wall','-Wformat','-Werror=format','-pthread',*inc,
              str(src/rel),'-o',str(o)]
    r=subprocess.run(argv,cwd=src,capture_output=True,text=True,env=env)
    (out/(name+'.log')).write_text(' '.join(argv)+'\n'+r.stdout+r.stderr)
    return name,r.returncode,(hashlib.sha256(o.read_bytes()).hexdigest() if r.returncode==0 else r.stderr[-1500:])
with cf.ThreadPoolExecutor(a.jobs) as ex: res=list(ex.map(comp,TUS))
for n,rc,h in res: print(n,rc,h if rc else h[:16])
if any(r[1] for r in res): sys.exit(1)
if not a.only:
    link=[NVCC,'-arch=sm_120a','-ccbin=/usr/bin/g++','-Xcompiler=-pthread','-L'+str(T/'lib64/stubs'),*[str(out/(n+'.o')) for n,_ in TUS],'-ldl','-o',str(out/'canary-miner')]
    r=subprocess.run(link,cwd=src,capture_output=True,text=True,env=env); (out/'link.log').write_text(' '.join(link)+r.stdout+r.stderr)
    if r.returncode: sys.exit(r.stderr[-800:])
    d=(out/'canary-miner'); print('native sha256',hashlib.sha256(d.read_bytes()).hexdigest(),d.stat().st_size)
