# P-12j: consent-bound user-pool fallback

M-TLS/M-CONFIG; R-TLS-001/002/003/004; T-TLS/T-CONFIG/T-INTENT/T-PRIVACY.

The ordinary first-user-pool path now accepts a saved **Prefer TLS** declaration in a TLS-enabled local build. Donation must still be explicitly off. This extends [P-12h](pool-tls-runtime.md) and uses the same fixed installed helper supplied by [P-12i](pool-tls-runtime-install.md).

## Selecting a policy

Configure the first user pool, then use Settings → Pools and TLS to supply its exact TLS URL. Prefer TLS without plaintext consent remains TLS-only. To allow fallback, separately supply a canonical `stratum+tcp://DNS:PORT` endpoint and confirm **ALLOW PLAINTEXT** in the existing contextual editor. Configuration, TLS and authorized plaintext endpoints must have the same normalized DNS host; only the explicitly supplied ports may differ. No port, region, account or pool is guessed.

An explicit `stratum+ssl://` input always remains strict, even when saved settings request Prefer TLS with consent. A conflicting saved TLS endpoint is refused. Donation and unused secure pool slots remain unsupported. Unchanged declarations may retain candidate URLs without activating them. Editing a mode or endpoint revokes the draft's consent, as before.

The parent freezes the effective TLS URL and, only when authorized, the separate plaintext URL for every worker launch and retry. Workers do not reread saved security preferences. A worker whose main configuration has acquired an explicit SSL URL cannot inherit the older fallback permission. Existing configurations are not migrated or rewritten.

Startup warns that plaintext exposes login/proof data and that a network attacker can provoke the allowed failures. A successful plaintext connection also produces a fixed warning in the ordinary run loop. Endpoint values, credentials and native diagnostics are not included in those warnings.

## Evidence that can permit fallback

The helper returns the original native Open status and phase in a versioned, strictly checked private RPC reply. The parent never infers a network failure from the generic TLS `Transport` or `Timeout` error.

| Observation from this exact TLS attempt | Result |
| --- | --- |
| Explicit DNS failure at Resolve, or an allowlisted network connection failure at Connect | May select the separately consented plaintext endpoint |
| A valid native timeout at Resolve/Connect, received before the parent's deadline | May select the separately consented plaintext endpoint if original budget remains |
| Certificate/name/trust failure, or any handshake error/timeout | No fallback |
| Socket allocation, local API/resource, unknown or mixed local/network failure | No fallback |
| Missing helper, malformed or trailing IPC data, nonzero helper exit, missing/late reply or parent timeout | No fallback |
| Stop, clock fault, foreign caller, lost native epoch, or application operation failure | No fallback within that attempt |

A failed Open reply is only a candidate until the helper's complete EOF and successful exit are observed within the original deadline. The old helper is then closed and settled. The one-use gate checks the frozen user scope, slot, attempt and TLS identity before a new plaintext helper may start. This permission is never inherited by donation.

Both connection stages share the original ten-second setup deadline; the remaining budget is not renewed at the transition. Native OS termination and scheduling remain subject to the documented process-owner limits, not a physical hard-deadline guarantee. A helper that cannot publish usable timeout evidence in time is conservatively treated as unknown.

## Connection and protocol ownership

The private protocol is now `CMTLSC2`/`CMTLSR2`. TLS and plaintext Open are distinct operations; the first operation freezes the connection's mode. `PlaintextOpen` is separate from `VerifiedOpen`, and replies cannot switch modes. Old helpers fail the protocol check rather than silently providing a weaker connection.

The plaintext path uses the helper's bounded nonblocking socket implementation. It does not call the older synchronous parent-side TCP transport. Positive partial writes retain their original buffer and offset inside the helper; a complete application chunk is acknowledged only when all its bytes have been sent. Plaintext EOF is ordinary TCP closure and carries no TLS authentication claim.

No send/read failure swaps an established session to plaintext. The canonical source closes that session, retains its existing PossiblySent/Unknown evidence and never replays an ambiguous submit. Every reconnect starts with a new TLS attempt and its own gate; even a previous plaintext connection grants no shortcut to TCP.

## Review and validation

Independent review identified and corrected two permission-sensitive defects during implementation: publishing a native failure before noticing corrupt trailing IPC/nonzero helper exit, and recreating relative budgets at the TLS-to-plaintext transition. The final design waits for a clean failed helper exit and passes one absolute deadline through both process owners. Review also preserved the existing meaning of Unchanged declarations with stored candidate endpoints.

Software checks on 2026-11-13 (local date):

| Environment | Result |
| --- | --- |
| Windows, runtime OFF | Full build and 475/475 CTests passed. The policy has 105 checks and actual inert CLI has 81. The later composition/protocol/artifact/document selection passed 5/5. After the equivalent sampling compile fix, seven sampling checks passed, but the latest CLI rerun is blocked by the security event described below. |
| Windows, runtime ON, private trust OFF | Ten low-level/loopback groups have passing results. One initial raw-TCP echo check treated an allowed reset on helper abort as a server failure; the test now accepts that close outcome while still checking all 32,779 echoed bytes and exact canonical submit counts. |
| Linux, runtime ON, OpenSSL 3.5.5 | Integrated selection passed 20/21 initially; the remaining one-shot lifecycle test lacked its separate fixture binary. Building that fixture and rerunning passed, as did the refreshed architecture contract. No production change was needed for the missing test artifact. |
| Runtime composition, both platforms | 142 checks passed on each. A test-only marker proves the second helper receives only the remainder after TLS consumes 3.5 seconds of a 6-second budget. Another marker triggers stop only after the plaintext factory is actually entered; cancellation remains latched. |
| Modern Linux native helper regression | 22 affected targets rebuilt; 174/174 CTests passed with zero skips. All fourteen binaries covered by the thirteen conditional linker-wrapper calls retain their original interposition flags. |
| Public system-trust handshake, both platforms | The native one-shot helper verified `prl-us.kryptex.network:8048` using system trust on Windows Schannel (2,927 ms) and Linux OpenSSL (1,571 ms), with zero application bytes, custom trust inputs or verification bypasses. This is a handshake-only positive, not ordinary source authorization or proof acceptance. |

The public endpoint is explicitly documented in the [official Kryptex overview](https://pool.kryptex.com/articles/pearl-miners-en). Checks took place at 2026-11-12 16:37–16:38 UTC; network and certificate results are observations at that time, not an availability guarantee. Receipt JSONs are retained locally as `build/p12j-public-handshake-windows.json` and `build/p12j-public-handshake-linux.json`.

Linux strict compilation also found and corrected a test-only conditional `initializer_list` lifetime defect and a fault-fixture indentation warning. Production-gate preparation exposed older-glibc test-harness portability failures. Optional harness functions now declare `maybe_unused`, and linker interposition is enabled only under the same glibc capability condition as the fixture body. GCC 15 also required an equivalent direct empty return before constructing a sampling result; its native lifecycle regressions are checked separately. Ubuntu 24.04/glibc 2.39 cannot launch the production duplex owner, so its interrupted build and failing process preflight are not full-gate passes. A compatible glibc >= 2.40 toolchain is required for that closure. No warning class, assertion or unavailable-platform skip is weakened.

The final Windows inert-CLI rerun did **not** pass: Windows Defender classified its newly built temporary executable as `Trojan:Win32/Bearfoos.A!ml` and quarantined it. The two recorded detection/action events match the two failing test copies; the second failure occurred while taking the initial file snapshot, before starting the CLI. The isolated earlier 475/475 result remains historical evidence, not acceptance of this final artifact. No protection setting, exclusion, filename workaround or test retry policy was changed. Static review found no test-side executable mutation, but that does not by itself establish a false positive. Windows final-artifact acceptance remains open.

Final production compilation/proof-gate closure is pending. The evidence separates pure policy and actual inert CLI admission, real process IPC with synthetic peers, native localhost TCP/TLS tests, public handshake-only checks and production compilation/proof gates. No public pool authentication, actual mining or installed-miner replacement is implied.

Donation TLS, published-preset migration, broader system-trust qualification, real pool/proof acceptance and release qualification remain separate work. Current official F2Pool PRL documentation still does not establish a usable secure donation tuple; see the [endpoint research](research/stratum-tls-endpoints.md). The existing donation destination and schedule are unchanged. P-12 remains PARTIAL.
