# Live terminal I/O and stop isolation (P-14z)

## Delivered scope and ordinary entry points

The ordinary idle `settings` command and `Supervisor` dashboard, Settings overlay,
plain logs and successful plain startup notice use the creation-bound
`ConsoleOutput` owner. This is normal wiring, not an optional development factory
or preview-only path. No new setting or opt-in macro is needed. The existing
mandatory `canary_preferences_worker` must remain beside the executable; there is
no PATH search, synchronous output fallback or silent retry when it is missing.
This module does not install or replace an existing miner.
Linux retains the existing creation-bound helper floor (glibc 2.40+ with the
required pidfd/kernel capability); no numeric-PID ownership fallback is added.
Older Linux compatibility remains unqualified.

Idle Settings still starts no mining. Existing navigation, independent drafts,
explicit Save receipts, redaction, CLI precedence and frozen session settings
remain authoritative. Worker admission waits for the first **nonempty** display
write acknowledgement as well as the existing bootstrap gates. Empty terminal
polls are not evidence that display output works. Output failure latches and
stops the session rather than restarting workers or replaying queued output.
Missing/broken output returns the existing idle refusal 3 or supervisor fault 8.
Plain summaries begin only after actual session activation, so an asynchronous
startup handshake cannot consume the first 15-second display slot before the
already-wired forecast/source owners exist. Revoked output builds no extra frame.
Successful one-shot activation also refreshes the first current dashboard
immediately instead of inheriting the pending screen's next-refresh deadline.

## Ownership, bounds and privacy

- The foreground queues memory only. One background owner holds the original
  intent/token/native epoch, one immutable issued frame and at most 32 queued
  jobs / 1 MiB. Individual bodies are limited to 262144 bytes. Only adjacent,
  complete, unissued screen snapshots may coalesce; issued output and log order
  cannot be rewritten.
- `CMTO1` carries a canonical sequential ticket and exact binary body length.
  `CMTA1` acknowledges a completed native stdout write; Windows `CMTA2` also
  carries bounded terminal geometry and one key. Neither receipt proves that a
  person saw the text. Malformed, oversized, surplus, wrong-ticket, late or
  unsolicited replies refuse, as do queue overflow and lost native ownership.
- The default operation budget is 2000 ms from original admission, including
  queueing/encoding/send/ack. Cancellation precedes post-worker-drain joining;
  helper retirement uses the existing 250-ms reap policy. These are cooperative
  software budgets, **not** hard kernel, process-creation or callback deadlines.
- The role is fixed at child creation. Console helpers do not construct the
  preference service, accept paths, open configuration files, fetch sources or
  control a worker. Only the already-inherited stdout is writable (including a
  user's explicit redirection). The private channel cannot upgrade an output role into a
  configuration service. Existing normal duplex callers retain their two-arg ABI
  and discarded-output behavior.
- POSIX freezes a duplicate of stdout before private pipe allocation, inherits
  only fds 0/1/2/3 and minimal locale environment, and never changes the parent's
  shared stdout nonblocking flag. Windows explicitly whitelists the private
  channel, original parent, NUL and selected display/input handles. No new user
  console is allocated or attached, and no desktop app is activated.
- Stop drops all unissued private frames/logs and key snapshots. It never replays
  them through another process or prints them synchronously. A separate,
  once-only cleanup may restore the cursor and emit only fixed status text or
  typed worker-count/session-seconds fields after workers drain. Cleanup has a
  200-ms write budget and cannot revive mining/output authority. Failure status
  must not be presented as a clean stop. Cleanup is best effort, not guaranteed
  screen delivery.
  A lost native epoch forbids cleanup too; absence of a footer is not exit proof.

## Why Windows also isolates input and geometry

Output-only isolation was insufficient on the tested Windows headless ConPTY:
native key polling and geometry/console-handle queries can wait behind retained
terminal output. Windows live loops therefore consume only the owner's cached
keys/geometry. The terminal helper performs those native operations. A second,
output-only finalizer is created and handshaken **before any display bytes**,
avoiding late console-handle duplication/query during pressure. Both helpers
remain owned and must retire. POSIX retains its existing nonblocking termios key
reader and creates the fixed cleanup helper only after the original exits.

This does not promise that a blocked terminal delivered Q or Ctrl+C. When input
cannot be observed, the original output deadline causes safe idle refusal 3 /
supervisor fault 8 instead of inventing an intentional stop receipt.

### Application return is not Windows terminal-host exit

An owned paused ConPTY can retain CRT/OS console-detachment work **after the
application entry or `Supervisor.run()` has returned and all owned workers and
helpers have retired**. The Windows regression therefore verifies an explicit
test-only actual-return receipt and the outer job's exact PID set before
releasing output. It then drains the terminal and separately requires actual
process/host exits and zero remaining job processes. Healthy cases require exit
0 and the expected final status. Retained cases permit the documented 3/8 safety
failure when input was not observed. This is not a claim of OS process exit
before output drain, a universal ConPTY latency bound or physical terminal
qualification. The receipt wrapper and environment marker exist only in test
fixtures, not in production entry/helper code.

Linux owned PTYs additionally require actual process exit before draining
retained stdout. Tests observe a sustained native fd-1 `write` barrier via
`/proc`, not merely a sleep, before issuing Q/SIGINT. They require exit 0 for
those observed intentional stops, no worker retry and actual helper retirement.

## Verification

New tests exercise canonical protocols, exact binary output, stop before start,
fault/overflow/coalescing, timeout, foreign caller/original epoch, parent death,
fixed sibling admission, idle/main and actual Supervisor behavior. Native owned
pipe tests cover descriptor/handle inheritance without draining blocked output
to make Stop pass. Windows uses an owned headless ConPTY, never the user's
console; Linux uses owned PTYs and inert CPU workers. No mining, hardware tuning,
game, public feed, pool, real account or installed-program operation is performed.

### CPU-fixture isolation correction

An expanded Linux sanitizer run exposed a real test-isolation defect: the old
header macro replacing the telemetry loader could be bypassed by inline/vtable
definitions from another linked translation unit. The forecast Supervisor fixture
actually loaded the WSL CUDA/NVML libraries; LeakSanitizer reported allocations
inside those external libraries. That attempt failed and is not CPU/no-driver
qualification. Earlier macro-based runs likewise cannot prove absence of driver
loading. No mining or hardware tuning was started, but the unintended driver
probe must not be described as an authorized or successful real-device test.

Affected fixtures now explicitly inject a uniquely named stats-only reader. It
reads only their owned loopback stats, leaves telemetry unknown and never calls
the native telemetry adapter; no production adapter or safety permission changes.
Linux affected CPU fixture links additionally wrap `dlopen` and fail with status
95 **before** any CUDA/NVML/NVIDIA/OpenCL/ROCm lookup. A dedicated positive check
loads the current image and seven negative names prove pre-load refusal. This
backstop is test-only, not a production restriction or a claim to intercept every
possible OS loading mechanism. Final guarded results are counted separately;
leak detection is not disabled to hide the failed attempt.

After explicit reader injection, Windows final-source default-off MSVC tests pass **472/472**. The
four new module CTest groups each pass **three additional repetitions**, including
ten healthy/retained headless-ConPTY cases per terminal test. Portable Python
discovery passes **175 tests / 24 explicit skips**, excluding 19 GCC-required
cases. The complete rebuild/run and subsequent repetitions pin identical **921
code inputs** (including the new test-only driver guard). No production deadline
or assertion is relaxed. The Linux-specific loader guard is not a Windows
interception claim; Windows fixtures use the explicit stats-only readers.

One earlier test image was blocked by Defender during a failed intermediate
run. It is not called a false positive or a pass, and no exclusion, restore,
rename or protection change is used. A later independent, reproduced dashboard
bug required a normal source rebuild; the resulting final test image passes the
full run. That result does **not** authorize restoring the old blocked image.
The [owner checklist](architecture/owner-actions.zh-CN.md) records this boundary.

Final-source guarded Linux strict-GCC ordinary and own-code ASan+UBSan/leak profiles
pass 53 protocol checks, four idle PTY cases, six Supervisor PTY cases, ten
owner scenarios (48 wrapper checks) and three entry failures. The two affected
production CPU units and all console consumers/helpers are freshly built;
43 dependency-verified unchanged qualified CPU objects are reused per profile.
External libraries are not sanitizer-instrumented. Both final focused runs pin
the same **921 inputs**, SHA256
`8da3212b585352c2ef466dcc9c1d6d55e4ec6ddf5bf362ff9d146d836807047f`,
as the final Windows run and guarded ordinary affected regression. The ordinary
selection passes native consumers, nine bootstrap modes, ownership/duplex,
all three original game cases, private bridge and existing Settings/startup,
market/forecast, credit/energy/billing and runtime PTYs. Python discovery passes
**194 tests / 19 explicit skips**. Besides the 43 unchanged core objects,
30 unchanged production extension objects are dependency-verified and reused;
this is not a fresh whole-core build. The corresponding guarded sanitizer
native/forecast prefix and remaining private/PTY selection pass separately.
Sanitizers are additional checks, not exhaustive memory/fault certification:
helpers retain their minimal environment/NUL diagnostics, and deliberately
terminated helpers cannot provide complete normal-exit leak inspection.

There is **no blanket all-green instrumented game qualification**. Exploratory
standalone game cases have deadline/readiness failures. One expanded guarded
`supervisor` integration attempt also fails six assertions; a diagnostic-only
copy and then the **unchanged** guarded current image pass, the latter three
times (**28 checks each**). This does not establish the cause or erase the
intermittent failure; reliable instrumented game timing remains **unqualified**
under P-11. No game policy, production deadline or timing-fixture change is
included to hide it. That fixture's only final change is explicit stats-only
reader injection. Original full game cases pass in ordinary Linux and Windows.
The separately passing actual-terminal module tests above establish this
terminal module's scope, not complete game-module acceptance.

Representative test entry points (with freshly built matching CPU artifacts):

```text
ctest --test-dir <windows-tests-build> -C Release --output-on-failure
python3 -B tests/test_console_output_terminal.py <inert-cli> <preferences-worker>
python3 -B tests/test_console_supervisor_terminal.py <cpu-supervisor> <preferences-worker>
python3 -B tests/test_console_output_process.py <console-owner-fixture> <preferences-worker>
python3 -B tests/test_console_output_entry.py <cpu-supervisor> <preferences-worker>
```

Normal CUDA/mining and clean-machine deployment/release are not built or
qualified by these CPU runs. Remaining early/standalone diagnostics, arbitrary
injected callbacks, disk/native/physical hard bounds, comprehensive privacy,
financial/game/TLS/localization integration and their external acceptance remain
separate. No personal input or live-operation permission is needed to close this
software module; see the [owner-action checklist](architecture/owner-actions.zh-CN.md)
for later modules.
