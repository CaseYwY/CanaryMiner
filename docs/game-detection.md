# Isolated readonly game-process detection (P-11b)

M-GAME; R-GAME-001/004/005/006, R-SAFE-001/002 and R-PRIV-001/002; T-GAME/T-INTENT/T-PRIVACY; D-63. `GameDetectionOwner` is default-off and freezes one explicitly selected process target for one parent session. The native helper makes readonly process observations; it does not start a game, inspect its memory, inject code, gather frames or operate a miner. Ordinary startup, settings and release installation do not activate this owner yet.

## Explicit target and supported scope

The target binds a private game key, an explicit adapter and a **complete ASCII executable basename** (1–128 letters/digits/dot/dash/underscore). Paths, command lines, substrings, launcher inference and automatic game discovery are not accepted. A basename identifies a process-name match, not executable authenticity or a measured game-experience baseline. An unrelated executable using that name may conservatively cause a pause.

- **WindowsSessionImage** uses Toolhelp's process snapshot and the helper's Windows session. Only matching names require limited-query/synchronization handles, a full image-basename recheck, creation time and a still-active process. ASCII case is folded. Another session is outside this adapter's scope; unknown session identity, failed matching-process access or failed enumeration is Incomplete.
- **LinuxUidNativeImage** uses native executable basenames in the helper's visible PID namespace. It reads bounded procfs status/stat/image metadata and includes processes with any real/effective/saved/filesystem UID matching the helper's effective UID. It checks creation identity, credentials and image again before accepting each selected task. Case is significant. Zombies/dead tasks and kernel threads are not active userspace games. Missing, changing or inaccessible metadata refuses completeness.

Linux requires ordinary procfs rooted at `/proc`, with visibility established from the **active mount ID** in the opened proc directory's fdinfo. Covered historical mounts at the same mountpoint are not the active view. `hidepid` restrictions, uncertain mount metadata or a non-proc mount are Unsupported; a failed image query is Incomplete. These adapters do not inspect Windows processes from WSL, translate Wine/Proton process names, discover another namespace/session or elevate privileges. A foreign adapter returns Unsupported, never Absent.

These are discrete polling observations within the stated scope, not a guarantee that no process can start between polls. A complete empty observation means Absent only in that scope. Any incomplete scan, capacity limit, permissions failure, malformed response or transport failure supplies **Unknown**, even if the partial scan found no matches. Unknown cannot confirm exit or clear an existing profit lock.

## Bounds, private identity and observation clocks

The one-shot `CMGDQ1`/`CMGDA1` frames are at most 2048 bytes, with canonical unsigned decimal fields, strict whole-input consumption and a full-request digest. The request includes generation, strictly increasing revision, frozen target, issuance clocks and scan bounds. The first enabled helper obtains a 128-bit private scope from the existing OS entropy helper; later flights must retain that scope. Entropy failure has no clock/PID/random-generator fallback.

The helper scans at most **32768 process rows** and retains at most **64 matching PID/creation pairs**. It returns only their sorted, scope-salted SHA-256 digest and count, never raw PIDs, image paths, command lines, UID values or unrelated names. The digest is an ephemeral private observation identity, not a durable device/user identifier and not a public stats field. It remains stable for the same matching instances within the owner. A new creation identity or changed instance set changes it; a PID or name alone is insufficient.

Both clocks are captured at scan beginning and finish. Observation freshness starts at the **beginning**, including entropy/inspection time; receipt and repeated snapshot reads never renew it. The client validates complete request binding, namespace continuity, time ordering inside the real request/receipt bracket, a two-second receipt-age cap and the total operation deadline after decoding. An overlong or inconsistent successful scan cannot publish presence/absence.

| Setting | Default | Bound |
|---|---|---|
| Native scan's cooperative budget | 250 ms | 25–1000 ms |
| Owned helper's deadline | 1000 ms | 50–2000 ms, at least the scan budget |
| Delay after completed flight | 500 ms | 100–10000 ms |
| Enumerated process rows | 32768 | 1–32768 |

The cooperative budget does not interrupt a blocking OS call. `LocalTaskProcess` supplies the isolated total deadline/cancellation and bounded reap attempt: Windows uses its one-child kill-on-close Job, Linux binds parent death before inspection. An Unreaped result is terminal for the owner; it is not permission to replace the unresolved helper. Genuine kernel-stall/global quarantine qualification remains open.

## Single-use owner and guard handoff

Start takes an already explicit parent-session generation; it cannot create mining intent. Exactly one background flight exists, with no queue, overlapping retry or missed-period catch-up. Each completion schedules the next ordinary poll after its own delay. Immutable snapshots preserve the original generation/revision/clocks. Failed work publishes a revisioned Unknown using the original issuance time, rather than retaining an apparently fresh Absent result. A disabled owner performs no process inspection; the disabled client path does not start a helper or obtain entropy.

Stop-before-start is terminal. `requestStop` revokes current observations and cancels the flight before any join; completed work cannot publish after it. `stop` joins separately. Clock reversal or cumulative wall/monotonic disagreement over two seconds latches ClockFault. Unreaped remains visible even when stop has priority. Target, source scope and lifetime cannot be reset on the same object.

`gameProbeDetection` maps a valid complete observation into the existing [episode guard](game-session-guard.md). The guard owns exit debounce and the ten-minute lock. Changes to a matching instance set conservatively withhold eligibility; they do not erase the previous episode. [P-11c](game-runtime.md) retains one guard and detector under the same explicit token; its future supervisor must keep that runtime across configuration review and worker restarts. The detector itself neither confirms exit debounce nor grants real coexistence or mining permission.

## Verification and remaining work

Tests exercise canonical frames, complete request/namespace binding, missing and substituted evidence, original observation age, disabled/no-helper behavior, cancellation/timeouts, terminal clocks, immutable stopped snapshots, exact-name matching and native identity parsing. Owned synthetic native target processes test presence, two instances, stable identities, removal and new creation; an independently killed owned parent checks helper termination. Ordinary artifacts are checked for absence of test-only parsing/stall hooks.

Executed evidence: Windows/MSVC focused **5/5**, with detector/owner **90**, native-name **8** and owned-process/lifecycle **35** checks. Linux/WSL/direct GCC passes detector/owner **90**, native parsing **24** and lifecycle **35**; the same selection passes **ASan+UBSan**, rebuilding the detector/owner/helpers/tests and reusing unchanged instrumented dependencies. Architecture/import guards pass. All complete Linux native lifecycle evidence used an isolated user/PID namespace; no lifecycle case was skipped in these runs.

The initial ambient Linux check correctly returned Incomplete because an image lookup was denied; the test had incorrectly assumed full desktop visibility. A private bounded probe confirmed the permission refusal still yields Unknown. Namespace validation then found covered procfs mounts at the same path, leading to active-mount-ID selection and eight additional parsing checks. Initial GCC test/source indentation warnings were also corrected. The normal Linux host test explicitly reports unavailable complete-scope checks while verifying Unknown; the lifecycle driver requests an isolated namespace and reports a CTest skip (77) if that capability is unavailable. It never counts an ambient unreadable scope as a complete absence/presence test.

Next: native target/mode review, supervisor admission and cooperative mining pause binding, followed by separately permitted real game baseline/experience validation. The fixed-lifetime guard/detector adapter is supplied by P-11c. Production economic qualification, durable episode/revocation semantics across parent replacement and physical pause deadlines remain open.
