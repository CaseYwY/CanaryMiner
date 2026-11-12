# Current user-pool transport status

P-12l adds read-only connection status to the ordinary dashboard, Details, Pools/TLS settings, `status` command and local stats API. It covers the first user pool. It does not change connection policy, saved declarations, fallback consent or donation routing.

## Meaning of the display

The configured endpoint and the effective policy for the running session are separate from the observed connection. Pools/TLS shows the session's frozen first-user host as well as its policy. Saving a setting does not change either. The current observation comes from the transport used by the canonical pool source, not from a URL prefix, a saved preference, a mining-rate report or a historical fallback counter.

- Verified TLS means the current transport completed its verified TLS open. It does not mean the pool accepted the login or any proof.
- Plaintext means the current connection uses TCP without TLS. A separately consented fallback reports its actual TCP port.
- Connecting means a new attempt has begun; the previous attempt's error and port no longer apply.
- Disconnected means the observed connection has ended. A fixed error category may explain the failure.
- Unknown means the observation is missing, invalid, unavailable or too old to establish the current state.
- Mixed is an aggregate display when workers have different known transport states or ports. It is not a transport mode.

The error field contains only closed categories. Native exception text, certificate contents, login values and credentials do not enter this new field. This does not claim to anonymize every existing log or stats field.

## Freshness and worker lifecycle

The run-loop owner publishes connection changes and fresh observations independently of mining-rate reports. Every reconnect starts a new observation. Close, stop and loss of native process ownership revoke a positive connection state. A donation run does not publish over the user-pool observation.

A positive observation expires after five seconds without a fresh transport observation. HTTP requests and repeated rendering do not refresh it. Slow requests consume that same age allowance. During a long search or a pause, Unknown is preferable to claiming a connection that has not been observed recently.

The supervisor combines every expected worker. It shows an agreed current state only when all workers supply fresh, consistent observations. Missing or stale workers prevent an aggregate Verified TLS result. Failed sampling, stopped workers and replaced worker generations cannot retain an earlier positive snapshot. The settings panel refreshes its current-status section while open without replacing the user's draft. On a short terminal, PgUp/PgDn scroll the read-only status and fields, including when the draft is invalid. Tab or arrow field navigation restores field focus.

These are local observations, not continuous physical monitoring of the remote server. A remote failure can remain unobserved until the next transport operation, and displayed observations can lag within the freshness bound.

Stopping immediately revokes the data used for subsequent snapshots. The existing console owner also cancels normal output when stopping; it attempts a fixed final cleanup after worker drain. A terminal may therefore retain its last frame during drain or if output is blocked. The five-second rule governs snapshots and active rendering, not a guarantee that an unresponsive or stopped terminal clears its pixels.

## Stats compatibility

The additive `pool.transport` object keeps the `canaryminer-stats-v1` schema identifier. Its fields are `state`, `error`, `port` and `age_ms`. The age is relative to the original observation; a consumer reconstructs a conservative local timestamp using the start of its HTTP request. A connected state has its actual port; other states do not claim an effective port.

Old documents without the object remain readable and produce Unknown. Unknown enum values, malformed fields and inconsistent combinations cannot become Verified TLS. Existing counter, rate, donation and pool-state semantics remain separate.

## Acceptance status

The producer's Linux focused run passed four groups, including 226 runtime transport checks. A subsequent 105-check run-loop test includes a real canonical source whose first connection fails before any mining report. The ordinary callback still publishes Connecting followed by Disconnected/Verification, and cleanup retains the error. Exact source hashes and commands are in `build/p12l-producer-final-receipt.json`.

The final Linux consumer run passed nine CTest groups in 56.06 seconds: stats, supervisor, display, settings, sampling, message catalogs, schema, PTY and fresh-process status. It includes 26 actual supervisor/HTTP/PTY assertions and six separate status processes. The PTY checks live state changes, expiry while HTTP remains healthy, immediate invalid-response revocation, frozen policy/host after saving another host, invalid-draft status access at 20x12 and owned worker shutdown. `build/p12l-consumer-linux-final-receipt.json` and its retained LastTest log bind the results to the tested files.

Independent review and entry-point verification identified these repairs:

- Short-terminal field focus made the new status header unreachable, especially with an invalid draft. Browse scrolling fixes that path. The frozen-host label also prevents a saved new endpoint from appearing to inherit the running connection's Verified TLS status.
- On natural worker exit, optional cleanup could delay the final publish while HTTP still served an earlier positive snapshot. `beginStop()` now changes the published state to draining and clears its transport observation before cleanup. An actual HTTP test holds fleet cleanup open; it failed on the original code and passes after the repair.
- The Windows `status` entry returns before mining startup initializes Winsock. The previous production binary exited 6 with `status: socket() failed` and sent no request to the local test server. The command now owns a balanced network initialization/cleanup scope. Separate-process tests prevent a test server's initialization from hiding this defect.

Windows focused validation passed ten CTest groups: eight in the initial run and the two affected groups after fixture repairs. The final supervisor test passed 329 checks, and the fresh-process status test passed six cases. The failed initial run is retained in `build/p12l-windows-focused-final-receipt.json`.

The current Windows TLS-enabled production build passed all 67 SASS comparisons and six actual main-program status cases: verified TLS, plaintext, verification error, older JSON, invalid transport metadata and an aged observation delivered through a slow response. The build produced no Unicode diagnostics. These CPU/loopback checks used explicit private test configuration, not the installed miner or a pool account. `build/p12l-windows-production-receipt.json` retains the exact commands, artifact hashes and production-input comparison. Its broader snapshot changed only in nine dashboard test fixtures during the build; all 451 production inputs were unchanged.

Failed preliminary checks remain separate: one CTest launch had a shell quoting error; a test initially expected a nonexistent Details heading; a wrapped-text assertion needed whitespace normalization; dashboard goldens needed the new status line and exact original EOF form; and the Python HTTP fixture needed the same HTTP/1.1 response version as the product server. None of those attempts is counted as a passing run. The full pinned Linux merge gate is still pending for this contribution.

P-12 remains PARTIAL. This display work does not complete donation TLS, migration, real pool login/proof sessions, GPU acceptance or release qualification. The prior Windows host-suite Defender block remains recorded in [P-12k](pool-tls-presets.md#final-software-evidence); it is not a passing Windows full suite.
