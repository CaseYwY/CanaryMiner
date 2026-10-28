# Public JSON HTTPS foundation (P-06a)

**Implemented, default off, not a live market integration.** Requirements: R-MKT-003 (partial), R-PRIV-002 and R-REL-001; families T-MARKET, T-INTENT and T-PRIVACY. No provider polling is started by the miner, no price/FX/credit facts are produced, and `FinancialView.enabled` remains false. Stratum and donation connections are unchanged. This helper is native host C++, not a GUI/browser runtime.

Follow-on [P-06b](market-data.md) now supplies the offline-tested adapters, exact calculation/cache owner and native projection. It does not change the default-off/no-supervisor-activation boundary or satisfy this document's trust/packaging release gates.

## Backend and ownership decision

Optional libcurl uses **c-ares**, Schannel on Windows and OpenSSL on Linux. The runtime refuses missing TLS/asynchronous-DNS/c-ares capabilities or an unexpected TLS backend. The curl ABI floor is 8.12, **not** a security approval for old unpatched releases. Development qualification used curl 8.23.0, c-ares 1.34.9 and the host's serviced native TLS facilities. Do not load another application's DLLs or install dependencies while mining.

`PublicJsonClient` is the low-level helper-side implementation. A multi interface and 50 ms polls make normal network waits cancellable, but **do not make synchronous OS trust operations cancellable**. In particular, Schannel may access AIA/CRL services while verifying a chain. Disabling revocation or certificate verification is not an acceptable timeout fix.

The application-facing `PublicJsonProcessClient` therefore owns one installed sibling `canary_public_json_worker` per operation, to be called from the future public-data background worker. It checks cancellation/deadline at 10 ms polling intervals and terminates only its owned helper on expiry. No shell or PATH lookup, mining arguments, inherited user environment or arbitrary URL is supplied. Windows uses Unicode process creation, an explicit handle allowlist, a suspended start followed by a one-process kill-on-close job, and only `SystemRoot` in the child environment. Linux uses `posix_spawn`, a dedicated process group, null stdin/stderr, an explicit minimal environment, and close-from file actions; the initial Linux capability requires glibc >=2.34 and fails closed otherwise.

An OS termination cleanup wait is capped at 250 ms per attempt. If the child has not exited, the same client retains its ownership and refuses another spawn until reaped. This is a bounded application wait, **not** a hard realtime guarantee on process creation, filesystem access, kernel termination, loader or scheduling. Destruction must not race a running `get`. Long-lived-client teardown/reaper handling under deliberately uninterruptible OS tasks is a remaining release qualification gate, not a tested success.

The helper path is application-owned, absolute and local; Windows UNC/device paths are rejected. Package it beside the executable in a protected installation directory. This is an ownership/timeout boundary, **not an OS security sandbox**. Native trust auxiliary fetches follow the OS/backend policy and are not governed by curl's payload-socket callback. Audit that egress, revocation, credential behaviour and loader graph before enabling a public runtime integration; do not describe the entire helper as network-allowlisted or credential-sandboxed.

## Request/response boundary

- Only four fixed unauthenticated GET identifiers: Gate `PEARL_USDT` ticker/minute candles and Kraken `USDTUSD` ticker/minute OHLC. Query bounds are validated integers, not provider-supplied URLs. The candle requests are transport scaffolding; dialect, inclusion and time semantics still need adapter qualification.
- HTTPS only, TLS >=1.2, peer/name verification, no automatic redirect or plaintext retry, no curl proxy, `.netrc`, cookies, login or automatic client certificate. Each option result is checked. TLS/name/trust failures remain distinct from HTTP/network errors.
- The payload socket's actual resolved IPv4/IPv6 address and port are checked before creation. Private, local, multicast, reserved, documentation and transition ranges are conservatively excluded; no second unchecked resolution. Linux socket creation preserves curl's `SOCK_CLOEXEC`/`SOCK_NONBLOCK` flags. A loopback exception exists only in a separately compiled fixture library.
- Default connect/whole-operation limits are 5/15 seconds; accepted body <=1 MiB and cumulative callback-delivered headers <=32 KiB, with caller limits allowed only within declared bounds. Library/TLS/kernel buffers are separate from these application limits; this is not a total-RSS promise.
- `Accept-Encoding: identity`; reject other content encodings and non-JSON media types. Count bytes before append, including chunked bodies. Failed/truncated/oversized/invalid responses publish no partial body or observation time.
- UTF-8, strict JSON grammar, duplicate-key rejection, <=16 container depth, <=20,000 lexical nodes, <=256 members per object, <=4096 raw bytes per string and <=128 bytes per numeric/literal token. Object-member limits bound the existing strict DOM's duplicate comparisons. Provider adapters must additionally validate identity, units, precision, finite domains, timestamps and error envelopes.
- Versioned, bounded `CMJSON1` IPC with exact body length; refuse malformed/trailing/over-limit frames or nonzero helper exit. One request per helper; stale bytes cannot be reused after cancellation.
- No transport retry. Numeric `Retry-After` is carried capped at 3600 seconds; HTTP-date values are not interpreted in this slice. Bounded retry/backoff, cache expiry, per-feed/global concurrency, observation clock policy, offline/manual inputs and immutable market publication belong to the next market-owner slice.

An HTTP observation time is not the time of the latest trade. This transport never turns an HTTP success, accepted-share count or balance into an income fact. Missing USD conversion and actual-credit provenance stay unavailable.

## Build and rollback

Normal builds use `CANARY_ENABLE_PUBLIC_HTTPS=OFF` and need no curl headers/library. The disabled client returns unavailable. CMake performs no automatic dependency download. With separately installed, reviewed development packages:

```sh
cmake --preset tests -B build/public-https -DCANARY_ENABLE_PUBLIC_HTTPS=ON -DCMAKE_PREFIX_PATH=<reviewed-prefix>
cmake --build build/public-https --target test_public_https test_public_json_process public_json_fake_worker public_https_probe public_https_fixture_worker canary_public_json_worker
ctest --test-dir build/public-https -R '^public_(https|json_process)$' --output-on-failure
python -B tests/test_public_https_loopback.py build/public-https/tests/public_https_probe --fixture-worker build/public-https/tests/public_https_fixture_worker
```

Add the platform executable suffix as needed; `--openssl` selects a trusted OpenSSL certificate-generation executable. Explicit opt-in `CANARY_HTTPS_LOOPBACK_TESTS=ON` registers the TLS fixture test in CTest. Default host tests do not require a listening port or network. The fixture CA/keys are generated in a temporary directory, never installed into the operator's root store, and deleted afterwards.

No config/schema/data migration is needed: this batch does not persist or enable a setting. Rebuild without the option to remove the backend/helper. Do not remove accepted history, credentials or runtime preferences as rollback.

## Executed evidence and boundaries

- Windows/MSVC and Linux/WSL/GCC: backend-enabled pure validation **103 checks each**; process/IPC lifecycle **49 Windows / 50 Linux checks** (Linux adds an explicit non-CLOEXEC descriptor assertion). Backend-disabled validation adds a separate unavailable-backend assertion (104 checks); the process tests still use only a synthetic worker.
- Both platforms: **25 direct TLS loopback cases plus 25 through the process boundary**. Cover success/SNI, chunking, redirects, HTTP/rate-limit errors, compressed/HTML/conflicting/oversized headers, declared/chunked oversized bodies, malformed/duplicate/deep/invalid-UTF-8 JSON, truncation, deadline, cancellation, forbidden loopback, wrong name, untrusted/expired certificate, native revocation-policy difference and plaintext-speaking peer. Failed TLS sends no HTTP application bytes. Ambient payload proxies are deliberately invalid and ignored.
- Process tests cover invalid/missing helper, invalid limits, bounded stalled-child termination, overflow/malformed/truncated IPC, nonzero exit, concurrent-owner refusal, stop-and-reuse, environment/descriptor isolation and Unicode/space-containing helper paths.
- Windows default-off focused CTest regression selection: **22/22 passed**, including both new suites and existing product/settings/history/intent/launcher/throttle cases. This is not the full production or Python discovery suite.
- Valid fixture TLS uses an explicitly trusted synthetic self-signed endpoint, without installing it globally. A subordinate certificate with no CRL fails under strict Schannel but passes OpenSSL's default chain policy. The test preserves this difference; it does **not** prove live CRL/OCSP or production revocation parity.
- No real DNS failure/delay fixture, public price request, account, wallet, GPU, game, pool, production CUDA build or long-run/clean-machine package gate has been executed for this slice. TLS1.0/1.1-only peers, native root-store behaviour, private-address auxiliary trust egress, kernel nontermination/reaping, helper crash stress and install/signing/loader policy remain release gates. This is not P-12 Stratum qualification.

The native loopback suites and full release gates are intentionally distinct. See [execution plan](architecture/execution-plan.md) for the remaining project objective.

## Development dependency provenance (not approved release locks)

Source archives were fetched over authenticated HTTPS into an isolated build workspace; no archive or library binary is committed. Recorded SHA-256:

- curl 8.23.0 `curl-8.23.0.tar.xz`: `8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5`.
- c-ares 1.34.9 `c-ares-1.34.9.tar.gz`: `9364b70a12eb59a06830fb488d9357f90100dff0d98b23ea3593a183d43e6f2f`.

These record downloaded development bytes, **not independently verified upstream signatures**. A Linux header-only OpenSSL development archive was checked against cached distribution SHA-256 metadata; the executable used the distribution runtime, not copied arbitrary host TLS binaries. Native package servicing and exact release source authentication/licence inventory remain mandatory. Notices copied from these two source archives are retained in `LICENSES/`; they do not cover every possible curl/OpenSSL dependency. Current upstream security/support status must be rechecked for release.
