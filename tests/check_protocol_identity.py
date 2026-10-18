#!/usr/bin/env python3
"""Protocol-identity gate: the bytes a pool or the Pearl verifier sees must equal the pinned golden values.

usage: check_protocol_identity.py PROBE_BINARY

Runs the probe on tests/fixtures/protocol_identity_fixture.txt and compares SHA-256 digests of the serialized PlainProof,
the mining.submit payload, the authorize payload (explicit agent) and the job-extra string with
tests/fixtures/protocol_identity_golden.txt (values produced on the pre-debrand seed tree). It also requires that the
miner's proof equals the Python-reference proof stored in the fixture. The pinned Pearl verifier build
(`pearl_mining`, Pearl 821c811d) is MANDATORY: put its directory on CANARY_VERIFIER_PYTHONPATH; the fixture proof is verified
with verify_plain_proof_v3 and the gate FAILS (never skips) when the verifier is unset, missing or rejects the proof.
"""
import hashlib,os,subprocess,sys,base64
HERE=os.path.dirname(os.path.abspath(__file__))
probe=sys.argv[1]
fx=os.path.join(HERE,'fixtures','protocol_identity_fixture.txt')
gold=dict(l.split() for l in open(os.path.join(HERE,'fixtures','protocol_identity_golden.txt')) if l.strip() and not l.startswith('#'))
out=subprocess.run([probe,fx],capture_output=True,text=True,check=True).stdout
vals={}
for line in out.split('\n'):
    k,_,v=line.partition(' ')
    if k: vals[k]=v
bad=0
for k in ('PROOF','SUBMIT','AUTH_EXPLICIT','EXTRA'):
    d=hashlib.sha256(vals[k].encode()).hexdigest()
    ok=d==gold[k]; bad+=not ok
    print(f'{k:14s} {"ok" if ok else "MISMATCH"} {d[:16]}')
ref=vals.get('PROOF_EQUALS_PYTHON_REFERENCE')
print('proof equals the Python reference proof:',ref); bad+=ref!='yes'
verifier='skipped'
vp=os.environ.get('CANARY_VERIFIER_PYTHONPATH')
if not vp:
    verifier='FAILED (CANARY_VERIFIER_PYTHONPATH is not set; the pinned Pearl verifier is mandatory)'
else:
    sys.path[:0]=vp.split(os.pathsep)
    try:
        import pearl_mining as pm
        toks=open(fx).read().split()
        hdr=bytes.fromhex(toks[4]); proof=bytes.fromhex(toks[-1])
        header=pm.IncompleteBlockHeader.from_bytes(hdr)
        pp=pm.PlainProof.from_base64(base64.b64encode(bytes.fromhex(vals['PROOF'])).decode())
        ok,msg=pm.verify_plain_proof_v3(header,pp); verifier='ACCEPTED' if ok else 'REJECTED: %s'%msg; bad+=not ok
        assert bytes.fromhex(vals['PROOF'])==proof
    except ImportError as e:
        verifier='FAILED (pearl_mining not importable: %s)'%e
print('pinned Pearl verifier on the miner-encoded proof:',verifier)
if not verifier=='ACCEPTED': bad+=1
sys.exit(1 if bad else 0)
