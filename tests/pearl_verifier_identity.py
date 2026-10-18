"""Resolve the loaded native oracle, not a package's Python re-export wrapper."""
from importlib.machinery import EXTENSION_SUFFIXES, ExtensionFileLoader
import os
import sys
import types


REQUIRED_EXPORTS = (
    "verify_plain_proof_for_cert_version", "PlainProof", "IncompleteBlockHeader",
)


def verifier_extension_path(package):
    """Identify one loaded extension exporting the exact objects the gate uses.

    PyO3 may report the package name in a callable's __module__, so that string
    alone is not provenance. Match re-exported objects against loaded native
    modules instead; do not import guessed filenames or hash __init__.py.
    """
    exports = tuple(getattr(package, name, None) for name in REQUIRED_EXPORTS)
    if any(value is None for value in exports):
        raise ValueError("Pearl verifier is missing required oracle exports")
    prefix = package.__name__
    candidates = set()
    for name, module in tuple(sys.modules.items()):
        if name != prefix and not name.startswith(prefix + "."):
            continue
        if not isinstance(module, types.ModuleType):
            continue
        spec = getattr(module, "__spec__", None)
        if not isinstance(getattr(spec, "loader", None), ExtensionFileLoader):
            continue
        path = getattr(module, "__file__", None)
        origin = getattr(spec, "origin", None)
        if not isinstance(path, str) or not isinstance(origin, str):
            continue
        path = os.path.realpath(path)
        if (path != os.path.realpath(origin) or
                not any(path.endswith(suffix) for suffix in EXTENSION_SUFFIXES) or
                not os.path.isfile(path)):
            continue
        if all(getattr(module, key, None) is value
               for key, value in zip(REQUIRED_EXPORTS, exports)):
            candidates.add(path)
    if len(candidates) != 1:
        raise ValueError("expected exactly one loaded native Pearl oracle; found %d" %
                         len(candidates))
    return candidates.pop()


if __name__ == "__main__":
    import pearl_mining
    print(verifier_extension_path(pearl_mining))
