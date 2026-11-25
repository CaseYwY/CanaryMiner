"""Check the actual public-data parent and helper images without running them."""
from pathlib import Path
import sys


def check_linkage(parent, worker):
    transport = b'CanaryMiner-PublicData/1\0'
    wire = b'CMJSON1'
    parent_image = Path(parent).read_bytes()
    worker_image = Path(worker).read_bytes()
    if wire not in parent_image or wire not in worker_image:
        raise SystemExit('public IPC contract missing from an expected native image')
    # MSVC can strip the transport code and still leave its DLLs in the import table.
    parent_imports = (b'iphlpapi.dll\0', b'wldap32.dll\0', b'libcurl.so', b'libcares.so')
    imports = parent_image.lower()
    if transport in parent_image or any(name in imports for name in parent_imports):
        raise SystemExit('public HTTP transport is linked into the parent image')
    if transport not in worker_image:
        raise SystemExit('public HTTP transport missing from the helper image')
    print('Public IPC remains in both images. The HTTP transport is confined to the helper.')


if __name__ == '__main__':
    check_linkage(*sys.argv[1:])
