#!/usr/bin/env python3
"""Per-kernel machine-code (SASS .text) digests of every cubin in a native or object file.

usage: sass_digest.py FILE OUT.json [--cuobjdump PATH]      (set CUOBJDUMP or pass --cuobjdump)
       sass_digest.py --compare A.json B.json

`cuobjdump -xelf all` extracts the device ELFs; each `.text.<kernel>` section's bytes are hashed. The only name
normalisation is the compiler's path-derived anonymous-namespace hash (`_GLOBAL__N__xxxxxxxx`), which depends on the
build path and does not appear in the machine code. Line-info sections are not part of `.text`, so -lineinfo and the
build path do not affect the digests.
"""
import sys,os,re,json,hashlib,subprocess,tempfile,pathlib
def u(b,o,n): return int.from_bytes(b[o:o+n],'little')
def digests(path,cuobjdump):
    out={}
    with tempfile.TemporaryDirectory() as d:
        subprocess.run([cuobjdump,'-xelf','all',os.path.abspath(path)],cwd=d,check=True,capture_output=True)
        for f in sorted(pathlib.Path(d).glob('*.cubin')):
            b=f.read_bytes(); assert b[:4]==b'\x7fELF'
            m=re.search(r'(sm_\d+[a-z]?)',f.name); arch=m.group(1) if m else f.name.split('.',1)[-1]
            shoff,shent,shnum,shstr=u(b,0x28,8),u(b,0x3A,2),u(b,0x3C,2),u(b,0x3E,2)
            secs=[(u(b,shoff+i*shent,4),u(b,shoff+i*shent+24,8),u(b,shoff+i*shent+32,8)) for i in range(shnum)]
            names=b[secs[shstr][1]:secs[shstr][1]+secs[shstr][2]]
            for nm,off,size in secs:
                n=names[nm:names.index(b'\0',nm)].decode()
                if n.startswith('.text.'):
                    out[arch+':'+re.sub(r'_GLOBAL__N__[0-9a-f]{8}','_GLOBAL__N__X',n)]=hashlib.sha256(b[off:off+size]).hexdigest()
    return out
if __name__=='__main__':
    if sys.argv[1]=='--compare':
        a,b=(json.load(open(x)) for x in sys.argv[2:4]); diff=[k for k in sorted(set(a)|set(b)) if a.get(k)!=b.get(k)]
        print(len(a),len(b),'EQUAL' if a==b else 'DIFFERENT',len(diff),diff[:8]); sys.exit(0 if a==b else 1)
    cu=os.environ.get('CUOBJDUMP','cuobjdump')
    if '--cuobjdump' in sys.argv: cu=sys.argv[sys.argv.index('--cuobjdump')+1]
    r=digests(sys.argv[1],cu); json.dump(r,open(sys.argv[2],'w'),indent=0,sort_keys=True); print(len(r),'kernels')
