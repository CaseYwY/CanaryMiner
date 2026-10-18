#!/usr/bin/env python3
"""Resolve `#if MACRO` / `#if !MACRO` / `#ifdef MACRO` / `#ifndef MACRO` blocks for a macro fixed to a value.
usage: resolve_macro.py MACRO VALUE(0|1) FILE...   (edits files in place; other conditionals are left untouched)
Used in Gate 1a to delete code that PEARL_ONLY builds never compiled."""
import re,sys
macro,value=sys.argv[1],sys.argv[2]=='1'
IF=re.compile(r'^\s*#\s*(if|ifdef|ifndef)\b(.*)$'); ELSE=re.compile(r'^\s*#\s*else\b'); END=re.compile(r'^\s*#\s*endif\b')
def classify(kind,rest):
    r=rest.strip()
    if kind=='if' and r==macro: return value
    if kind=='if' and r=='!'+macro: return not value
    if kind=='ifdef' and r==macro: return True
    if kind=='ifndef' and r==macro: return False
    return None
for path in sys.argv[3:]:
    src=open(path,encoding='utf8',newline='').read().split('\n'); out=[]; stack=[]; removed=0; blocks=0
    for l in src:
        m=IF.match(l)
        if m:
            c=classify(m.group(1),m.group(2))
            if c is None: stack.append(None); 
            else: stack.append([c,False]); blocks+=1; removed+=1; continue
        elif ELSE.match(l) and stack and stack[-1] is not None:
            stack[-1][1]=True; removed+=1; continue
        elif END.match(l) and stack:
            top=stack.pop()
            if top is not None: removed+=1; continue
        active=all((e is None) or (e[0]!=e[1]) for e in stack)
        if active: out.append(l)
        else: removed+=1
    open(path,'w',encoding='utf8',newline='').write('\n'.join(out))
    print(path,'blocks resolved',blocks,'lines removed',removed)
