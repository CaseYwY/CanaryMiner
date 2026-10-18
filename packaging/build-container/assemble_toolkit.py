#!/usr/bin/env python3
"""Assemble the CUDA 13.1 redistributable components into one toolkit root (bin, include, lib64, nvvm, notices).

usage: assemble_toolkit.py DOWNLOAD_DIR TOOLKIT_DIR
DOWNLOAD_DIR holds the extracted component archives (one top-level directory each, e.g. cuda_nvcc-linux-x86_64-13.1.115-archive).
The layout matches the one the qualified natives were built with: files under bin/, include/ and nvvm/ keep their path, lib/
becomes lib64/ (with a lib -> lib64 link), everything else goes to notices/<component>/. Two components may not ship different
bytes at the same path. DRAFT: not exercised in Gate 1b-1 (no container was built).
"""
import hashlib
import json
import pathlib
import shutil
import sys

download, toolkit = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
toolkit.mkdir(parents=True)
records = {}
for source in sorted(p for p in download.iterdir() if p.is_dir()):
    name = source.name.split('-linux-x86_64-')[0]
    for f in sorted(source.rglob('*')):
        if f.is_dir():
            continue
        rel = f.relative_to(source)
        parts = rel.parts
        if parts[0] in ('bin', 'include', 'nvvm'):
            dest = toolkit / rel
        elif parts[0] == 'lib':
            dest = toolkit / 'lib64' / pathlib.Path(*parts[1:])
        else:
            dest = toolkit / 'notices' / name / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        if dest.exists() or dest.is_symlink():
            assert dest.read_bytes() == f.read_bytes(), 'conflicting file ' + str(dest)
        elif f.is_symlink():
            dest.symlink_to(f.readlink())
        else:
            shutil.copy2(f, dest)
        records[str(dest.relative_to(toolkit))] = dict(component=name, sha256=hashlib.sha256(f.read_bytes()).hexdigest())
(toolkit / 'lib').symlink_to('lib64', target_is_directory=True)
target = toolkit / 'targets' / 'x86_64-linux'
target.mkdir(parents=True)
(target / 'include').symlink_to('../../include', target_is_directory=True)
(target / 'lib').symlink_to('../../lib64', target_is_directory=True)
(toolkit / 'toolkit-files.json').write_text(json.dumps(records, indent=1, sort_keys=True) + '\n')
print(len(records), 'files assembled')
