# Mandatory preference helper archive software (P-14s)

M-RELEASE/M-CONFIG/M-PRIVACY; R-REL-001/002, R-PRIV-002, R-UI-007; T-RELEASE/T-PRIVACY/T-CONFIG; D-176.

This is a packaging-software correction, **not a produced, installed or qualified mining release**. The native live-settings binding requires `canary_preferences_worker[.exe]` beside the actual miner module. The legacy release scripts copied only the miner even when the helper already existed. Owned Windows and Linux copy-fragment probes reproduce that omission before the correction, twice on each platform; no CUDA build, installed miner, driver, pool or publishing command is involved.

## Fixed two-program layout

Both scripts now require and copy the fixed CPU helper, include it in their exact top-level layout checks, and describe its sibling placement in the archive README. Linux strips and sets executable modes on both copies. The helper is not a second miner, an independent operator command, a PATH lookup or permission to enable other development helpers.

`packaging/release/verify_artifacts.py` inspects both staged images **without executing either one**. It requires bounded regular x86-64 ELF/PE images (at most 256 MiB each), explicit nonempty UTF-8 private deny-list input, and an unambiguous OFF cache for energy runtime, worker-control runtime and accelerated donation test policy. Every present `CANARY_ENABLE_*` cache entry must be typed Boolean OFF. Duplicate/conflicting required cache entries and non-Boolean capability substitutions refuse; the donation schedule is not changed.

The existing generic/private-pattern scanner sees full UTF-8 and both alignments of UTF-16 LE/BE, not just long printable ASCII runs. Build-machine path categories refuse separately. Only the exact declared public fee token and existing approved public metadata exemptions remain; an address prefix is not exempt. Failures withhold original bytes, paths and inspector output rather than echoing a discovered secret. Inputs stay preserved; nothing is repaired, retried, deleted, uploaded or automatically redacted into an approved release.

Linux uses `objdump -p/-T`, not `ldd` or the inspected program. Dependencies must belong to the declared system C/C++ set; only the miner may additionally import the driver's `libcuda.so.1`. The CPU helper may not import it or CUDA toolkit libraries. Both images must contain the undefined `pidfd_spawn` native reference rather than an older-header build that silently compiled out creation-bound process capability. Windows uses the actual build linker's sibling `dumpbin.exe /DEPENDENTS`; ordinary and delay imports must belong to the declared system/release C++ runtime set, with `nvcuda.dll` permitted only for the miner. This is direct import inspection, not transitive loader/trust or dynamically loaded module qualification.

After inspection both complete original image byte strings are checked again. This detects the exercised mid-inspection replacement, including a first-image change during the second inspection. It is not filesystem immutability, signature verification, an ACL/symlink-race sandbox or every undetected ABA proof.

The scripts require a new work directory instead of recursively deleting a caller-selected existing directory, preserve failed/private build inputs, and refuse existing archive/checksum outputs. This does not certify concurrent/malicious output writers, source-tar extraction, Windows archive determinism or a complete release supply chain. The validator and README are selected from the same explicitly extracted source tree rather than an unrelated invocation-directory copy.

## Honest Linux capability floor

The reported glibc requirement is the greater of **2.40** (the current source's native-ownership compilation contract) and the GLIBC symbol versions in **both** shipped images. It is not merely the miner's old symbol floor or an Ubuntu 22.04/24.04 compatibility promise. The glibc ABI list records `pidfd_spawn` itself at GLIBC_2.39; that API introduction must not be confused with this project's stricter 2.40 header guard. Kernel pidfd spawn/signaling/wait support, waitable SIGCHLD and permitting container/seccomp rules are independently required. C++ runtime versions, clean-machine distributions, native GPU/driver and actual minimum-kernel qualification remain open.

## Evidence and remaining gates

Synthetic ELF/PE headers and real isolated native metadata tools test helper omission, missing/linked/invalid images, dependency refusal, explicit cache policy, private UTF-16/short literals, exact public exemptions, changed inputs and private failures. The actual scripts' artifact-copy fragments execute only in owned temporary directories; complete release builders are not executed. Exact current-source results, regressions and privacy checks are recorded in the [execution ledger](architecture/execution-plan.md) after completion.

The whole-host regression also exposed a pre-existing test-only causality error: a 200-ms post-COMMIT acknowledgement-loss case could time out before the helper reached COMMIT. A controlled 350-ms startup delay reproduces that mismatch through the actual native helper/protocol/ledger. The test now observes the existing post-COMMIT fixture marker before cancellation; an additional delayed-helper CTest exercises the same complete history suite. Only test scheduling/phase assertions change, not production timeouts, transactions, recovery policy or mining behaviour.

These checks do not prove nvcc/SASS reproducibility, protocol/donation equivalence in a real miner, a clean-machine installer, transitive/dlopen loader trust, actual device/game behavior, physical durability/latency or release approval. The overall roadmap and R-REL-001/002 remain partial.

References: [DUMPBIN /DEPENDENTS](https://learn.microsoft.com/en-us/cpp/build/reference/dependents?view=msvc-170), [glibc 2.40 x86-64 ABI list](https://github.com/bminor/glibc/blob/release/2.40/master/sysdeps/unix/sysv/linux/x86_64/64/libc.abilist), [waitid/P_PIDFD](https://man7.org/linux/man-pages/man2/waitpid.2.html), [Python 3.13 subprocess](https://github.com/python/cpython/blob/v3.13.14/Doc/library/subprocess.rst).
