#!/usr/bin/env python3
"""Deny-list scan for secrets and private paths in the product tree.

usage: python tools/check_secrets.py [--root DIR] [--history]

Scans every tracked file (git ls-files; all files if the directory is not a git repo). With --history it also scans every
blob reachable from any ref (`git log --all`), so a leak that was later removed is still found.

Two kinds of patterns:
  * built-in generic patterns (committed): any bech32-looking PRL payout address that is not a clearly marked placeholder,
    Windows/WSL/Linux home-directory paths, private-key / token shapes;
  * local patterns read from an UNTRACKED file (never committed): `.secrets.local` in the root (or the path in the
    CANARY_SECRETS_FILE environment variable), one literal per line, '#' comments allowed. Put exact payout addresses,
    fee addresses, worker names, pool logins, user names and host names there.

Findings print file, line and the pattern *name* only, never the matched text. Exit 1 if anything is found.
"""
import argparse,os,re,subprocess,sys

PLACEHOLDER=re.compile(r'^prl1(q{4,}|example|-fixture|x{6,}|test|placeholder|your|replace|0{6,})',re.I)
GENERIC=[
 ('prl-address', re.compile(r'\bprl1[a-z0-9]{20,}\b')),
 ('windows-user-path', re.compile(r'[A-Za-z]:[\\/]+Users[\\/]+(?!<|\{|\$|%|Public|user\b|you\b|example)[A-Za-z0-9._-]+',re.I)),
 ('wsl-user-path', re.compile(r'/mnt/[a-z]/Users/(?!<|\{|\$|Public|user\b|you\b|example)[A-Za-z0-9._-]+')),
 ('linux-home-path', re.compile(r'(?<![A-Za-z0-9_.-])/home/(?!<|\{|\$|user\b|you\b|example\b|runner\b)[a-z][A-Za-z0-9._-]*/')),
 ('private-key-block', re.compile(r'-----BEGIN [A-Z ]*PRIVATE KEY-----')),
 ('api-token-shape', re.compile(r'\b(?:ghp|gho|ghs|github_pat)_[A-Za-z0-9_]{20,}\b|\bAKIA[0-9A-Z]{16}\b')),
]
TEXT_LIMIT=8*1024*1024
# Public by owner decision (H-018b): the fee destination is an address by design. Each entry is an exact file and an exact
# line shape; no other line of those files, and no other file, is exempt from any pattern.
PUBLIC_LINES={
 'src/product/fee.h': re.compile(r'^\s*#define CANARY_FEE_DESTINATION "prl1[a-z0-9]{20,}"\s*$'),
 'README.md': re.compile(r'^- Donation address \(public PRL address\): `prl1[a-z0-9]{20,}`$'),
}
# Public by owner decision (2026-10-18): the maintainer's GitHub handle. Only this exact token is exempt from the local
# deny-list (it is masked before local matching); longer or shorter variants, paths and other accounts still match.
PUBLIC_TOKENS=('bucnazty2020','bucnazty90')
PUBLIC_TOKEN_RX=re.compile(r'(?<![A-Za-z0-9_])(?:%s)(?![A-Za-z0-9_])'%'|'.join(map(re.escape,PUBLIC_TOKENS)))

def local_patterns(root):
    path=os.environ.get('CANARY_SECRETS_FILE') or os.path.join(root,'.secrets.local')
    pats=[]
    if not os.path.exists(path):
        print('check_secrets: FAILED - the untracked local deny-list is missing (create .secrets.local in the repository root or set CANARY_SECRETS_FILE); refusing to run with generic patterns only')
        sys.exit(2)
    for l in open(path,encoding='utf8',errors='replace'):
        l=l.rstrip(chr(13)+chr(10))
        if l and not l.lstrip().startswith('#'): pats.append(l)
    if not pats:
        print('check_secrets: FAILED - the local deny-list is empty')
        sys.exit(2)
    return path,pats

def scan_text(name,data,local,findings):
    try: text=data.decode('utf8',errors='replace')
    except Exception: return
    base=name.split(':')[-1]   # history names are 'history:<rev>:<path>'
    for i,line in enumerate(text.split('\n'),1):
        if base in PUBLIC_LINES and PUBLIC_LINES[base].match(line): continue
        for pn,rx in GENERIC:
            for m in rx.finditer(line):
                if pn=='prl-address' and PLACEHOLDER.match(m.group(0)): continue
                findings.append((name,i,pn))
        masked=PUBLIC_TOKEN_RX.sub('<public-handle>',line)
        for k,lit in enumerate(local):
            if lit in masked: findings.append((name,i,'local-pattern-%d'%(k+1)))

def git(root,*args):
    return subprocess.run(['git',*args],cwd=root,capture_output=True)

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--root',default=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..')); ap.add_argument('--history',action='store_true')
    a=ap.parse_args(); root=os.path.abspath(a.root)
    path,local=local_patterns(root)
    findings=[]
    is_git=git(root,'rev-parse','--git-dir').returncode==0
    if is_git:
        files=[f for f in git(root,'ls-files','-z').stdout.decode('utf8','replace').split('\0') if f]
    else:
        files=[os.path.relpath(os.path.join(d,f),root).replace(os.sep,'/') for d,ds,fs in os.walk(root) for f in fs if not {'.git','build','__pycache__'} & set(os.path.relpath(d,root).split(os.sep)) and f not in ('.secrets.local','.smoke.local')]
    n=0
    for f in files:
        p=os.path.join(root,f)
        if not os.path.isfile(p) or os.path.getsize(p)>TEXT_LIMIT: continue
        data=open(p,'rb').read()
        if b'\0' in data[:8192]: continue
        n+=1; scan_text(f,data,local,findings)
    if a.history and is_git:
        revs=git(root,'rev-list','--all').stdout.decode().split()
        seen=set()
        for rev in revs:
            tree=git(root,'ls-tree','-r','-z',rev).stdout.decode('utf8','replace').split('\0')
            for ent in tree:
                if not ent: continue
                meta,_,name=ent.partition('\t'); sha=meta.split()[2]
                if sha in seen: continue
                seen.add(sha)
                data=git(root,'cat-file','blob',sha).stdout
                if b'\0' in data[:8192] or len(data)>TEXT_LIMIT: continue
                scan_text('history:'+rev[:8]+':'+name,data,local,findings)
        print(f'history scan: {len(revs)} commits, {len(seen)} distinct blobs')
    print(f'check_secrets: scanned {n} files; {len(local)} local patterns loaded from {"an untracked file" if local else "nothing (no local file)"}')
    byname={}
    for _,_,pn in findings: byname[pn]=byname.get(pn,0)+1
    for f,i,pn in findings[:200]: print(f'  {f}:{i}: {pn}')
    if findings:
        print('FOUND',len(findings),'finding(s):',', '.join(f'{k}={v}' for k,v in sorted(byname.items()))); sys.exit(1)
    print('clean'); sys.exit(0)
if __name__=='__main__': main()
