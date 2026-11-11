# Build-only diagnostic preview and explicit new-file export (P-14n)

M-PRIVACY; R-PRIV-001/002; T-PRIVACY/T-RELEASE; D-170.

`canary-miner diagnostics` is an idle, closed-schema command. It returns before configuration, preferences, platform/device discovery, runtime owners, mining and networking. It reads none of those sources. Default stdout is a canonical LF JSON preview with **every optional group excluded**. Stderr names the SHA-256 of exactly those bytes. `--include-build` explicitly includes only enumerated platform, compiled architecture, compiler family and canonical numeric public version; arbitrary version labels become `null` rather than being echoed.

The schema is [diagnostics-v1](../config/diagnostics-v1.schema.json). It excludes identity, configuration, runtime, network, logs, financial and game-activity data, with no additional properties. Report content contains no path, host/device/account identifier, date, random tracking identifier, credentials, user input, performance, consumption or activity. This deliberately small report is **not** a raw-log collector, a comprehensive system diagnostic or runtime/hardware/release qualification.

## Consent and preservation

1. Preview with the intended optional groups; inspect the JSON and digest.
2. Invoke the same binary and selection with `--export NAME.json --confirm SHA256`. No persistent consent or automatic export is stored. The digest binds exact bytes, not a human-review proof, signature, credential or security authorization token.
3. Choose an unused portable ASCII basename in the operator-selected current directory. Path traversal, subdirectories, absolute paths, Windows device names, non-ASCII and alternate streams are refused. No directory is created. Mining/global/configuration flags, unknown flags, duplicates and conflicting preview/export requests refuse generically without echoing arguments or paths.

Export **creates only a new file**. It never opens an existing target to write, overwrites, appends, auto-retries, removes or repairs it. POSIX pins the selected directory, uses `openat` with `O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC` and mode `0600`, verifies the opened regular file and flushes file/directory. Windows uses `CreateFileW(CREATE_NEW)`, no handle inheritance or sharing, verifies a regular non-reparse disk handle and calls `FlushFileBuffers`. Windows inherits the directory's ACL; private ACL certification is not claimed. Platform create semantics are documented by [Linux open(2)](https://man7.org/linux/man-pages/man2/open.2.html) and [Microsoft CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew).

Cancellation before creation creates no file. Any failure/cancellation after creation reports an **unknown outcome**, retains the possible partial new file and does not silently delete or retry it. Existing targets, including symlinks/FIFOs on POSIX, stay unchanged. The operator must inspect an unknown result before choosing any further action.

## Scope and limits

Filesystem/console calls are synchronous **only in this idle command**, never supplied as a live mining/safety callback. There is no hard native I/O latency, physical durability, network-filesystem, malicious directory-writer, OS/cloud-sync prevention, installed-miner or release qualification claim. The application itself makes no upload/network request. Restrict the chosen directory using the OS; software cannot promise that an externally configured sync/backup agent will not copy a file.

This command does not make arbitrary raw logs safe to export or read private data merely to redact it. Broader native Privacy controls, collection/disclosure, retention/delete workflows, credential-vault references and sensitive runtime export remain separate work. It does not complete R-PRIV-001/002 or the whole roadmap. Exact executed cross-platform test results belong in the execution ledger after they finish, not before.

Final tight Linux ordinary/own-code ASan+UBSan checks pass **1425 report / 158 parser / 32 native export / 72 actual CLI**, with twenty additional repetitions of all three unit programs. Windows default-off full CTest passes **424/424**, all four diagnostic cases repeat twenty times, and the direct native writer passes **18** Windows-applicable checks. POSIX-only mode/symlink/FIFO/native size-limit fault assertions are not counted as Windows evidence. The twelve-schema suite and language/pool CLI regressions pass; broader native/privacy/cleanup counts are recorded separately in the execution ledger.
