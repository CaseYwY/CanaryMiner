# Secure transport backends for CanaryMiner

**Evidence retrieved:** 2026-10-28. **Scope:** Windows/Linux, C++17, certificate-verified Stratum byte streams and bounded, unauthenticated HTTPS JSON feeds. This is design research, not a statement that TLS or a feed integration has been implemented or qualified.

**Evidence standard:** official platform/library documentation, upstream source, and pool-operated public documentation only. Context7 was used to locate library documentation; the API contracts and upstream source cited below take precedence over generated summaries. Public documentation was read without using an operator's wallet, account, configuration, credentials, or telemetry. No mining port was contacted, scanned, or probed; no TLS handshake, pool login, application message, share submission, trading operation, or live market-data request was performed. Public pool addresses below are documentation evidence, not observed live connectivity.

## 1. Recommendation

**Recommended first implementation:** retain a small shared C++17 byte-stream contract, implement **Schannel on Windows and OpenSSL on Linux for raw Stratum**, and use **libcurl on both platforms for public HTTPS feeds**. Build Windows libcurl with Schannel and Linux libcurl with the same supported OpenSSL family used by the raw-stream adapter. This avoids both a handwritten HTTP client and a second Windows-specific HTTP implementation, while exposing the exact TLS readiness state needed by the existing bounded submission path.

This is an engineering recommendation, not a requirement imposed by any cited library. Its principal tradeoff is two native TLS adapters. A complete native split—Schannel/WinHTTP on Windows, OpenSSL/libcurl on Linux—is viable when avoiding an additional Windows redistribution dependency is more important than sharing HTTP behavior. A common **libcurl-only application-facing transport layer** is also credible: libcurl explicitly supports arbitrary application protocols over a TLS connection using connect-only mode. However, its public raw-send/receive interfaces return `CURLE_AGAIN`, not an explicit TLS `WANT_READ`/`WANT_WRITE` direction. That is a qualification gap for this miner's strict nonblocking requirements, not evidence that libcurl cannot carry Stratum.[^curl-connect][^curl-send][^curl-recv][^curl-openssl-source]

Do not add a custom cryptographic implementation, invoke an installed `curl` command as the transport, or silently depend on whichever TLS DLL happens to be discoverable. No candidate eliminates certificate verification, byte framing, bounded buffering, cancellation, dependency updates, or application-level acknowledgement tracking.

### Options and costs

| Approach | Raw Stratum / HTTPS | Advantages | Maintenance and packaging cost |
| --- | --- | --- | --- |
| Native platform split | Windows Schannel / WinHTTP; Linux OpenSSL / libcurl | Windows TLS and HTTP are OS components; explicit control of raw TLS state; Windows native trust integration. | Two raw TLS implementations **and** two HTTP adapters; duplicate error, timeout, redirect, size-limit and cancellation tests. Linux still needs maintained OpenSSL/libcurl packages. Schannel record buffering is substantial application code.[^schannel-init][^schannel-encrypt][^winhttp-read] |
| One common transport API: libcurl | Connect-only TLS for Stratum; normal HTTPS for feeds; Schannel on Windows, OpenSSL on Linux | One application-facing maintained C library; shared verification/connection setup and HTTP policy; no HTTP request is required for connect-only mode. | libcurl still needs a TLS backend and possibly other libraries. Raw-stream retry direction is not exposed by the cited public API; long-lived socket scheduling, deadlines and framing remain application work. Backend behavior must be qualified independently.[^curl-connect][^curl-send][^curl-recv][^curl-libs] |
| **Recommended hybrid** | Native raw TLS; common libcurl HTTPS | Exact raw-stream state plus one HTTP adapter; no OpenSSL redistribution on Windows when libcurl uses Schannel. | libcurl build/update ownership on Windows; two TLS adapters remain. Prefer a minimal static Windows libcurl build if release-size and runtime-policy review permit; otherwise harden app-private DLL loading. |
| Shared OpenSSL raw TLS plus libcurl HTTPS | OpenSSL on both OSes, libcurl for HTTP | One raw TLS implementation and explicit retry direction. | Not a single application-facing dependency; Windows gains OpenSSL libraries/provider and trust-store integration obligations. Reasonable alternative if identical TLS implementation matters more than native Windows servicing/trust.[^openssl-host][^curl-certs] |

All these C APIs can be wrapped behind RAII ownership in C++17. Keep transport implementation in ordinary host C++ translation units where possible, rather than spreading backend headers through CUDA/protocol headers. The repository's production build uses C++17, while its CPU-test profile returns before production target construction; dependency wiring must cover both deliberately.[^repo-build]

For Linux OpenSSL, **3.5 LTS is a defensible supported-family target**: upstream currently lists support through 2030-04-08. This is not a recommendation to freeze a patch version, nor to reject a distribution's separately maintained security backports. Select and record an actually supported patched build at implementation/release time.[^openssl-support]

## 2. Existing repository seams and incompatible assumptions

The following observations refer only to checked-in source, not private settings or runtime state:

* `PearlPoolTransport` already separates bytes from Pearl framing. Its result distinguishes bytes, would-block, EOF and error, but has **no retry-interest mask, TLS handshake state, buffered-plaintext readiness or certificate-error category**. Its socket implementation performs synchronous IPv4-only resolution/connect.[^repo-transport]
* `sendLineUntil` waits for writability **before** each send and changes socket blocking mode; `readLineUntil` waits for readability before receiving. Pre-submit intake also uses zero-time socket readability to decide whether queued work exists. TLS can violate all three assumptions: reads can need writes, writes can need reads, and plaintext can already exist inside the TLS backend when the socket is quiet.[^repo-pool][^openssl-error][^openssl-pending]
* Presets currently encode plaintext ports, and `parsePoolSpec` explicitly refuses `stratum+ssl://`. Adding a TLS class alone therefore does not make preset/configuration/CLI behavior secure.[^repo-config]
* Donation routing is independently compiled and overwrites the user's pool destination/account fields when entering its lane. Future transport fields must also be constructed explicitly for that lane, **not accidentally inherited from a copied user `RunOptions`**. No destination account, worker, payout address or digest is needed in public transport documentation.[^repo-fee]

Preserve the protocol's existing inbound limits, request correlation, session invalidation, gzip dialect negotiation and stale-work protections. TLS is a byte-stream change, not permission to weaken those checks or redesign mining semantics.

## 3. Non-negotiable security and consent policy

The following is a **proposed product contract**.

### TLS identity and protocol policy

1. Default to **verified TLS**, with TLS 1.2 as a minimum and TLS 1.3 allowed where supported. Do not force TLS 1.2 as a maximum or re-enable obsolete protocols to improve pool compatibility. Windows Schannel TLS 1.3 starts with Windows 11/Windows Server 2022; a backend cannot supply protocol versions the OS lacks.[^schannel-protocols]
2. Verify both the certificate chain and the configured endpoint name. Send DNS SNI using that name, not a resolved address, reverse-DNS result or arbitrary certificate name. SNI routes the handshake; it does **not** authenticate the peer. A literal IP requires an appropriate IP subject alternative name; do not disable name verification to accommodate it.[^schannel-init][^schannel-source][^openssl-host][^curl-verifyhost]
3. Use Windows native trust for Schannel. On Linux, use a documented distribution CA source or an explicitly managed CA bundle with a refresh process. A missing/stale/unreadable trust source is an actionable failure, not grounds to skip verification. OpenSSL's successful `SSL_CTX_set_default_verify_paths` return does not prove its default locations contain usable certificates: missing default locations can still count as success.[^curl-certs][^openssl-trust]
4. Do not enable insecure switches, validation callbacks that always return success, automatic client-certificate selection, or TLS early data. Schannel/libcurl can otherwise expose a user's client certificate unexpectedly; early data is inappropriate for replay-sensitive login/share messages.[^schannel-creds][^openssl-verify][^curl-ssloptions]
5. Preserve a distinction between chain/name/time/revocation errors, local trust/backend errors, network errors, protocol errors and unsupported capability. Do not advertise identical revocation guarantees across backends: Schannel curl performs CRL checks by default, while the ordinary OpenSSL trust/hostname setup cited here does not establish equivalent online revocation behavior.[^curl-certs][^curl-ssloptions][^openssl-verify]

### Explicit, endpoint-scoped plaintext consent

Represent policy separately from the port: for example, `tls_required` and a separately acknowledged `allow_plaintext_fallback` option bound to one pool entry and its explicitly specified plaintext endpoint. These are proposed names, not existing configuration keys. An explicit plaintext-only selection may be another consented mode; it must display the same warning. A bare address must not silently mean plaintext, and a known-pool suffix must not authorize port arithmetic or a downgrade.

| Event | Required behavior, including when plaintext fallback was previously consented |
| --- | --- |
| Valid verified TLS | Continue encrypted. Do not send Stratum application bytes before handshake and verification complete. |
| Wrong name, untrusted/expired/not-yet-valid/revoked certificate, or failed certificate-policy check | **Never downgrade.** Mark the attempt as a security failure. A security failure must not cause the failover scheduler to select a plaintext pool as an indirect fallback. Independently configured verified-TLS failover may proceed. |
| Revocation status unavailable under a policy that requires it; missing CA store; missing/unsupported TLS backend | Fail closed. Do not reinterpret this as “pool has no TLS.” Do not set `NO_REVOKE`, best-effort revocation, or ignore-certificate flags to recover silently. |
| Generic TLS alert, reset during handshake, or generic SSL-connect error | Not proof of absent TLS; no automatic downgrade. Unclassified errors remain ineligible. |
| Documented plaintext-only pool selected explicitly, or a narrowly classified pre-certificate network failure on a configured TLS endpoint | Plaintext may be used **only** under the entry's recorded consent and explicit endpoint mapping. Warn before any login is sent. Network faults can be attacker-induced; consent acknowledges that loss of protection rather than proving fallback safe. |
| Consent missing, endpoint changed, or consent belongs to another pool/lane | No plaintext. Require a fresh scoped decision. |

Latch a certificate/security failure for the connection/failover sequence: a later timeout must not erase it and unlock plaintext. Clearing the latch requires a deliberate policy reset or a successfully verified TLS recovery, not an ordinary retry. Do not reuse TLS sockets, queued messages or half-finished authorization across a TLS/plaintext switch.

HTTPS feeds have **no plaintext fallback**, regardless of pool consent. Donation has its own transport policy and consent scope. Permission to use plaintext for the user's pool never authorizes plaintext donation traffic.

## 4. Correct nonblocking byte-stream behavior

### Shared interface contract

Replace the operation-equals-readiness assumption with an explicit state machine:

`Resolving -> Connecting -> Handshaking -> VerifiedOpen -> Closing/Closed/Failed`

Proposed operation results carry: application bytes consumed/produced, progress versus retry versus EOF/fatal outcome, **required readiness mask** (`read`, `write`, or both), whether buffered work can progress without a socket event, and a structured error category. A retry has no new acknowledged application bytes. The transport owns any pending ciphertext and retains stable write input until the backend permits release.

* Keep sockets nonblocking throughout their owned lifetime. Preserve one absolute monotonic deadline for each connect/handshake/request/shutdown budget; fragments and TLS control messages must not restart it. Check cancellation in bounded waits. Resolution must also be bounded/cancellable; a TLS timeout does not fix synchronous `getaddrinfo`.
* Attempt buffered receive/protocol progress before waiting, subject to a per-tick work budget. A ready socket is not necessarily a complete TLS record, and a complete TLS record is not necessarily a JSON line. Multiple lines per record and one line split across records must behave identically to the plaintext fake transport.
* Keep separate bounded encrypted-input, decrypted-input and encrypted-output storage where the backend requires it. Count buffered decrypted work in pre-submit intake. Do not let a quiet socket conceal a buffered replacement job.
* Retain one pending write operation across retries; advance offsets only by newly reported consumption. Never regenerate an already encrypted Schannel record after a partial socket write. Backpressure must stop producing more records when output reaches its cap.
* Serialize operations on a TLS context through one owning thread/state machine. No concurrent handshake/close on the same context. Use bounded shutdown and distinguish clean TLS closure, truncated transport EOF and fatal errors; none is an acknowledgement of a submitted request.[^schannel-init][^schannel-decrypt][^openssl-error][^openssl-write]

**Submission evidence needs a TLS-aware revision.** TLS-accepted plaintext is not proof that a peer received a complete request. Conversely, a retrying TLS write can already have produced partial wire traffic even if it reports no newly accepted plaintext. Track “write never attempted” separately from “TLS write started,” application-byte consumption, and any backend-output flush state available. On interruption after a write attempt, conservatively retain **outcome unknown** until a correlated pool reply resolves it; do not blindly replay a share because a TLS API returned zero new bytes. Existing socket-byte evidence cannot simply be relabeled “bytes received by pool.”[^repo-pool][^openssl-write]

### Windows: Schannel stream adapter

* Acquire outbound credentials using modern `SCH_CREDENTIALS` on the supported OS baseline. Keep automatic server validation/name checking; prohibit `SCH_CRED_MANUAL_CRED_VALIDATION` and `SCH_CRED_NO_SERVERNAME_CHECK` in production. Set `SCH_CRED_NO_DEFAULT_CREDS` so no client certificate is selected implicitly. Supply the configured server name to `InitializeSecurityContext`; upstream curl's Schannel source explicitly uses this argument for SNI as well.[^schannel-creds][^schannel-init][^schannel-source]
* Use `ISC_REQ_STREAM`, confidentiality and appropriate allocation/connection flags; validate final returned attributes. Loop handshake tokens through `InitializeSecurityContext`. Preserve incomplete input on `SEC_E_INCOMPLETE_MESSAGE`, retain `SECBUFFER_EXTRA` bytes, and completely flush owned output tokens, including a final token on `SEC_E_OK`. Free SSPI-allocated buffers using the matching API.[^schannel-init][^schannel-extra]
* Query `SECPKG_ATTR_STREAM_SIZES`; respect `cbMaximumMessage`, header and trailer sizes. `EncryptMessage` produces records for the application to send; it is not a socket-write operation. Keep a record plus its socket-write offset until fully flushed.[^schannel-encrypt]
* On receive, preserve incomplete records and excess encrypted bytes. `DecryptMessage` can succeed with no application data. Handle `SEC_I_RENEGOTIATE` as documented control/handshake processing, including post-handshake TLS messages, rather than assuming that all such results are legacy renegotiation attacks. Apply the same boundedness and security policy throughout.[^schannel-decrypt][^schannel-extra]
* `SCH_CREDENTIALS`/`TLS_PARAMETERS` require Windows 10 1809 or later. Explicitly disable protocols below TLS 1.2 for this client while retaining system cipher policy; do not modify registry-wide TLS settings. Runtime-supported OSes must be separately declared and tested; the API's minimum OS is not a product support promise.[^schannel-creds][^schannel-parameters]

### Linux: OpenSSL stream adapter

* Use a client `SSL_CTX`, explicitly enable `SSL_VERIFY_PEER`, load the intended trust source, set the TLS 1.2 minimum, set `SSL_set1_host` and, for DNS names, `SSL_set_tlsext_host_name`. Check return values. OpenSSL's verification mode defaults to `SSL_VERIFY_NONE`; successful encryption alone is not sufficient.[^openssl-verify][^openssl-trust][^openssl-host][^openssl-version]
* With nonblocking socket BIOs, drive `SSL_connect`, `SSL_read_ex` and `SSL_write_ex` using the **actual** `SSL_get_error` result. **Any** of these I/O operations can need either direction. Empty the owning thread's error queue before the operation and call `SSL_get_error` immediately on that same thread without intervening OpenSSL calls.[^openssl-error]
* On `SSL_ERROR_WANT_READ`/`SSL_ERROR_WANT_WRITE`, repeat a pending write with the same arguments. Moving-buffer mode relaxes the pointer, not the content/length requirement. With `SSL_MODE_ENABLE_PARTIAL_WRITE`, a successful partial result completes that operation; advance by exactly the reported bytes and begin the remainder as a new operation. Without it, success normally represents the whole supplied application buffer. Never interpret a retry as a fresh independent write.[^openssl-write]
* Consume `SSL_pending` plaintext before a socket wait. `SSL_has_pending` is only a hint: it can remain true for an incomplete/control record while another read still needs network input. Bound progress attempts instead of busy-looping on either function.[^openssl-pending]
* Distinguish `SSL_ERROR_ZERO_RETURN`/`close_notify` from unexpected EOF. Do not set `SSL_OP_IGNORE_UNEXPECTED_EOF` merely to make closure appear clean; close the session and invalidate unfinished framing/correlation state. Do not call `SSL_shutdown` after the fatal conditions for which OpenSSL forbids further I/O.[^openssl-error]

## 5. What a common libcurl raw-stream backend would require

The documented mechanism is real and useful, but is **not HTTPS Stratum**:

1. Build an internal `https://` URL from the selected TLS pool authority, set `CURLOPT_CONNECT_ONLY=1L`, enable peer/name verification, require TLS 1.2+, and disable HTTP ALPN for this raw protocol. Do not pass `stratum+ssl` directly as an unknown libcurl URL scheme. In connect-only mode libcurl performs connection/proxy setup but **no HTTP data transfer**. Disable implicit proxies for the initial implementation, rather than introducing proxy authentication/traffic as a side effect.[^curl-connect][^curl-alpn][^curl-proxy]
2. Drive connection setup with a multi handle or a cancellable bounded worker. After successful setup, use **only `curl_easy_send`/`curl_easy_recv` for application bytes**. The active socket is for readiness observation, not raw `send`/`recv` that bypasses TLS. libcurl owns closure and associated resources.[^curl-send][^curl-recv][^curl-socket]
3. If setup used multi, keep its easy handle added for the entire connect-only lifetime; removing it makes the raw-send/receive functions stop working. Connect-only connections are not reusable through the ordinary connection cache.[^curl-connect]
4. Handle successful partial sends and preserve the same pending data on `CURLE_AGAIN`; the send API requires the same buffer content and a retry length no smaller than the original request. Drain receive repeatedly until `CURLE_AGAIN` before waiting, because libcurl/the TLS library may already hold data. Successful zero-byte receive means closure.[^curl-send][^curl-recv]
5. Own all **post-setup** application deadlines and cancellation. A connect-only transfer finishing does not establish a lifetime timeout for arbitrary future raw I/O.

**Unresolved directional-readiness gate:** the send/receive manuals describe waiting for writable/readable sockets respectively, but OpenSSL explicitly permits the opposite dependency. In the inspected curl revision, its OpenSSL adapter records `CURL_SSL_IO_NEED_RECV`/`SEND` internally for these conditions and returns `CURLE_AGAIN`; the raw public API does not return that mask. Its public signatures and easy implementation do not supply a per-call direction result. Do not extract private structs, borrow the TLS handle to perform independent I/O, assume a completed multi transfer will schedule every later raw I/O need, or poll both directions continuously: an always-writable socket can create a CPU spin while TLS actually needs input.[^openssl-error][^curl-openssl-source][^curl-easy-source]

A curl-only implementation must demonstrate bounded, non-spinning, correct operation under cross-direction retries and post-handshake control traffic on **both** chosen backends, or obtain an upstream-supported scheduling contract. Until then, the direct stream adapters are the safer release baseline for this specific contract. This is the reason for the recommendation, rather than a claim that connect-only TLS is unsupported.

## 6. Bounded public HTTPS JSON feeds

Use a separate `PublicJsonClient` and connection/worker ownership from mining lanes. Feed failure must not block job intake, mining, share submission or shutdown. It may change data freshness/availability, never the selected pool or donation transport.

### Request and response contract

These are **initial engineering limits**, not provider facts or measured production requirements:

| Boundary | Proposed first-slice policy |
| --- | --- |
| Request target | Explicit reviewed origin/path allowlist, HTTPS only, ordinary GET, no userinfo/fragment, bounded URL/query length; no arbitrary URL received from another feed. Public-feed destination policy rejects private, loopback, link-local and otherwise reserved addresses in both address families; validate the addresses actually used without a second unchecked resolution. Loopback exceptions belong only to the isolated test client. |
| Authentication | No wallet/account fields, API keys, cookies, `.netrc`, Authorization headers, ambient proxy credentials or automatic client certificates. Do not perform a login or credential discovery. |
| Redirects | Disabled. Treat 3xx as a provider-adapter change to review; never follow HTTPS-to-HTTP or cross-origin redirection automatically. |
| Time | Example connect budget 5 seconds, complete operation budget 15 seconds, explicit cancellation and bounded waits. Use a qualified asynchronous DNS resolver build; a synchronous resolver can defeat timeout promises with `NOSIGNAL`. |
| Concurrency | Example two active requests globally, one in flight per feed, bounded queue and response cache. No unbounded retry or task spawning. |
| Body | Example 1 MiB maximum accepted JSON body; enforce cumulatively in the write callback before append/allocation. Reject overflow and abort immediately. `Content-Length` is not the authority. |
| Headers | Example 32 KiB cumulative application-accepted headers including status lines, interim responses and trailers; enforce in the header callback. libcurl may buffer a complete header line up to its own `CURL_MAX_HTTP_HEADER` before invoking that callback, so this is **not** a 32 KiB bound on total library allocation. |
| Compression | Initially request `identity` and reject non-identity content encoding. If gzip is later enabled, bound **decoded** bytes as well as transfer/time budgets; do not trust the compressed `Content-Length`. |
| Parse | Parse only an accepted success response of the expected JSON media type. Bound nesting, node/array counts and individual strings in addition to raw bytes. Reject malformed/trailing JSON, duplicate keys where ambiguous, invalid numeric forms and non-finite/out-of-domain values. |
| Publication | Publish a validated immutable snapshot with source identifier, unit/currency, source time if supplied, observation time and freshness status. Keep previous good data separately labeled stale; failures are not zero prices or zero network values. |

For libcurl, explicitly set `CURLOPT_SSL_VERIFYPEER=1L`, `CURLOPT_SSL_VERIFYHOST=2L`, a TLS 1.2+ minimum, `CURLOPT_PROTOCOLS_STR="https"`, no automatic redirects, connect/total timeouts, and `CURLOPT_PROXY=""` unless a future explicit proxy policy is approved. Check **every** option result; unavailable security controls are startup/build capability failures, not ignored settings. Disable unused protocol/authentication features in the shipped build where practicable.[^curl-verifypeer][^curl-verifyhost][^curl-version][^curl-protocols][^curl-timeouts][^curl-proxy]

Use `CURLOPT_WRITEFUNCTION` and `CURLOPT_HEADERFUNCTION` with length-aware, exception-safe callbacks. Never allow a C++ exception across a C callback boundary. Return an abort/error result rather than partially accepting an over-limit chunk. Count every delivered header block, not just the final status. `CURLOPT_MAXFILESIZE_LARGE` is defense in depth, **not** a substitute for these application counters: historical versions differ in in-progress and decompression enforcement.[^curl-write][^curl-header][^curl-maxfile]

The compression rule is deliberate: servers can send encodings different from those requested, and auto-decoding makes callback bytes differ from compressed `Content-Length`. If compression is added, qualify the exact library build, dependencies and expansion limits instead of enabling every supported encoding casually.[^curl-encoding]

Retry only idempotent public GETs, with a bounded attempt count, capped backoff and cancellation; respect provider throttling and stop repeated failures. Treat HTTP status, provider JSON error fields, schema validity and data staleness independently. TLS authenticates transport, not a market quote's economic reliability.

### Documented public-feed example, not a PRL integration claim

Kraken documents unauthenticated `GET https://api.kraken.com/0/public/Ticker`, returning JSON with `error` and `result`. Its `pair` parameter selects a market; omitting it asks for **all** tradeable pairs, so a bounded adapter should request an explicitly reviewed pair instead. This demonstrates a public JSON contract without account or trading operations. **This research does not establish that Kraken lists Pearl, that a PRL pair exists, or that this endpoint supplies the desired PRL valuation.** No live response, price, trading instruction or availability claim is included.[^kraken-ticker]

The initial slice can implement only transport plus fixture adapters. Selecting PRL price, exchange-rate, network-statistics or tariff providers still requires first-party endpoint/schema, instrument identity, access-terms and freshness review. An authenticated account API is not a substitute for a missing public feed.

### If Windows must instead use WinHTTP

Use secure requests with strict default certificate validation; do not set `SECURITY_FLAG_IGNORE_*`. Disable redirects explicitly—WinHTTP's default blocks HTTPS-to-HTTP but still permits other redirects. Disable ambient authentication for public feeds, set phase timeouts plus an application-wide deadline/cancellation mechanism, and read into bounded caller-owned chunks rather than allocating from a remote length. Asynchronous `WinHttpReadData` has different output-pointer rules and requires the buffer to remain valid until completion. These differences are part of the extra adapter/test cost, not reasons to relax the common feed contract.[^winhttp-options][^winhttp-read]

## 7. Pearl preset and donation endpoint evidence

**“Documented” means visible in public pool-operated documentation. It does not mean DNS, reachability, chain trust, name matching, current protocol support or successful Stratum dialect operation was tested.** All live certificate/handshake properties remain unverified.

| Route | Publicly documented endpoint/capability | Safe implementation conclusion |
| --- | --- | --- |
| Kryptex Pearl, current North America preset | Official Pearl miner overview lists the North America host `prl-us.kryptex.network:7048` and explicitly says: “For SSL, use port 8048 on the same hosts.” The pool page also publishes a `stratum+ssl` example on port 8048.[^kryptex-ports][^kryptex-pool] | Candidate TLS preset: `stratum+ssl://prl-us.kryptex.network:8048`. Preserve the documented plaintext endpoint only as a separately consented mapping. Do not infer TLS on 7048 from a scheme replacement or generalized port rule. |
| HeroMiners Pearl, current east-US preset | Official Pearl page lists `us2.pearl.herominers.com:1200`. HeroMiners' official settings guide states that the same port supports `stratum+tcp` and `stratum+ssl`.[^hero-pool][^hero-tls] | Candidate TLS preset: `stratum+ssl://us2.pearl.herominers.com:1200`. Same-port support is documentation-level evidence, **not proof of a trusted certificate for this regional name**. |
| F2Pool Pearl | Its official Pearl tutorial and launch announcement publish `stratum+tcp://pearl.f2pool.com:5500`; the tutorial's examples are plaintext. No Pearl-specific certificate-verified TLS endpoint was established in the retrieved first-party material.[^f2-tutorial][^f2-launch] | **TLS endpoint unverified.** Do not invent an SSL port/hostname, copy a different coin's SSL settings, or claim TLS is unavailable everywhere. The secure default must report a capability gap; explicit plaintext consent is a separate choice. |
| Compiled donation lane | Checked-in routing selects F2Pool independently of the user's pool list. Public endpoint evidence is consequently the same F2Pool Pearl evidence above, not a separate verified donation service.[^repo-fee][^f2-tutorial] | **Verified TLS donation routing is blocked by the unverified F2Pool TLS endpoint.** Do not inherit user plaintext consent, fall back silently, or redirect donation to a different pool/account. Defer/skip the donation window under existing failure policy and resume user work until a verified route or separately approved donation-specific policy exists. |

Some first-party miner instructions include certificate-disabling flags. Such examples are **not evidence of authenticated TLS** and must not be copied into this security design. In particular, the F2Pool tutorial's `--no-cert-validation` example does not establish an SSL endpoint or justify an insecure CanaryMiner mode.[^f2-tutorial]

**Evidence limitations:** a separate F2Pool consolidated pool-information page was blocked by its web protection during full retrieval; it is not used to assert a Pearl TLS port. The accessible Pearl tutorial/announcement provide affirmative plaintext evidence, not exhaustive evidence of the absence of TLS. No account dashboard was consulted, no provider contacted, and no endpoints guessed or probed.

## 8. Licensing, secure packaging and servicing

| Component/choice | Verified licensing or platform fact | Required project work / cost |
| --- | --- | --- |
| Schannel / WinHTTP | Microsoft documents OS DLLs/import libraries for these APIs; they are Windows facilities, not vendored open-source transport packages.[^schannel-decrypt][^winhttp-read] | Use the OS-serviced components; do not redistribute replacements for Windows system DLLs. Declare supported OS versions, test native trust/policy behavior, and account for OS servicing. This note is not a review of SDK/runtime redistribution terms. |
| libcurl | Upstream curl license permits use/modification/distribution and requires its copyright and permission notices in copies; it does not require publishing CanaryMiner source.[^curl-license] | Add the exact shipped license/notices to the release inventory; record build options, TLS backend, resolver, version and source digest. A permissive top-level license does not cover every optional dependency automatically. |
| OpenSSL 3.5 | Apache-2.0; distribution obligations include providing the license, retaining applicable notices, marking modified files and carrying applicable NOTICE attribution. The license includes a patent grant with conditions.[^openssl-license] | Preserve exact applicable upstream license/NOTICE material and modifications/provenance. Static linking does not remove notice or patch obligations. Keep CanaryMiner's own license distinct. |
| Optional dependencies / CA material | curl lists separate libraries for TLS, compression, HTTP/2, asynchronous DNS and other features.[^curl-libs] | Prefer only needed HTTP(S), TLS and resolver features. Each enabled codec, resolver, HTTP/2/3 stack, provider and bundled CA source adds licensing, provenance, vulnerability and packaging review. No assumption that the whole graph inherits curl's license. |
| Static Windows libcurl + Schannel | No separate application libcurl/TLS DLL is needed for that build design. | Larger executable and full rebuild/release for dependency security updates; qualify runtime/CRT linkage and actual imported DLLs. This reduces one loading risk but is not a general supply-chain guarantee. |
| Shared app-private libraries | Windows searches for a DLL's dependencies by module name even when the parent DLL was loaded by full path.[^dll-order] | Protect the **whole** dependency graph and installation directory. Never search the working directory or arbitrary `PATH`, and never download missing libraries during execution. |

For explicit Windows loads, use a resolved app-private path and suitable `LoadLibraryExW` search flags, or establish restrictive `SetDefaultDllDirectories`/`AddDllDirectory` policy. Use system-only resolution for explicitly loaded OS components. Harden transitive loads as well as the first DLL. A call made in `main` cannot retroactively secure startup imports; inspect/package those dependencies separately. A portable directory writable by an attacker cannot become trusted just by choosing a full path.[^dll-security][^dll-order]

On Linux, prefer declared distribution dependencies for a distribution package, or a complete reviewed private runtime for a portable release. Do not copy arbitrary host shared objects or invent loader search paths involving the current directory. Inventory any OpenSSL provider/configuration files actually needed; do not make an unmanaged plugin search directory part of the trust boundary. A portable OpenSSL build also needs an explicit CA-source strategy, not the build machine's default CA location.[^openssl-trust]

Release gates should record exact source/archive hashes and versions, dependency/license inventory, compiler/runtime compatibility, enabled protocols/features, selected backend, CA-source policy and a clean-machine packaging test. Reject a runtime library missing required features. Monitor curl/OpenSSL security updates and supported-family dates; rebuild pinned binaries promptly when affected. No fixed byte-size or performance estimate is established by this research.

## 9. Recommended implementation slice and exit gates

1. **Contract first, no public network:** add typed security policy, structured errors, TLS states, readiness interests, buffered-read awareness and conservative submission evidence to the existing injectable seam. Keep protocol framing and pool fixtures. Replace blocking-mode toggles/readiness-before-operation assumptions; make deadlines monotonic and cancellation testable.
2. **Native verified stream adapters:** implement Schannel/OpenSSL behind that contract, with test-owned TLS peers and trust fixtures. Keep platform APIs in host C++ files. Gate all application writes on verified handshake completion. Demonstrate partial ciphertext handling, opposite-direction retries, pending plaintext, timeout and shutdown behavior before preset integration.
3. **One bounded HTTPS client:** add minimal libcurl builds and a fixture-based `PublicJsonClient` implementing the limits above. Qualify resolver/timeouts and cancellation. Start with reviewed uncompressed JSON, redirects disabled, no credentials and no arbitrary URL entry point. Feed adapters remain separate from transport.
4. **Preset/configuration migration and donation isolation:** add documented TLS candidates, explicit endpoint mappings and scoped plaintext consent, with visible status/error reasons. Old plaintext settings must not silently opt in to fallback. Mark the F2Pool TLS capability gap; keep the donation lane from bypassing the default or copying user transport policy. Do not change donation destination or accounting policy as part of transport work.
5. **Package and qualify both OSes:** wire dependencies into production and CPU-test profiles; preserve offline unit tests. Inventory notices/CA/provider artifacts and qualify clean-runner loader behavior. Only after these gates should a separately authorized live TLS interoperability exercise be considered; it is outside this research and does not require sending mining messages to verify a certificate.

If a one-libcurl raw backend is still preferred, make it a separate, bounded feasibility slice against the same tests. Passing a happy-path handshake and newline echo is insufficient to resolve its directional-readiness gate. Do not couple that experiment to shipping the independently useful bounded HTTPS client.

## 10. Deterministic test plan

**These are proposed tests, not executed results.** Unit tests use injected clocks, resolver/socket/backend scripts and fixed synthetic data. TLS integration peers bind only to loopback and use fixture certificates/trust. Windows native-trust integration belongs in a disposable test user/VM, not by adding certificates to an operator's trust store. Certificate-time unit tests use a fixed verification clock/fault injector; integration validity is controlled in the isolated runner. No test requires a real pool, wallet, account, GPU or external feed.

| ID | Deterministic scenario and required assertion |
| --- | --- |
| T01 | Script DNS/connect/handshake progress and cancellation at every boundary; the absolute deadline is never renewed by fragments, and resources close once. Cover all resolved-address candidates and a failed first address. |
| T02 | Correct CA/name succeeds; wrong DNS name, wrong IP SAN, untrusted root, expired/not-yet-valid certificate and missing peer certificate fail before application bytes are written. Capture the expected DNS SNI in the local peer. |
| T03 | Missing/empty/unreadable CA source and unsupported TLS/security option produce distinct capability/configuration failures, never an insecure retry. Cover OpenSSL default-path “success but empty” behavior. |
| T04 | Force TLS 1.0/1.1-only peers to fail; TLS 1.2 and supported TLS 1.3 succeed. Unsupported OS/backend capability is reported without changing machine-wide TLS policy. |
| T05 | Force write -> `WANT_READ` and read -> `WANT_WRITE`; wait for the requested direction, retry the same pending operation, and prove bounded callback/poll counts with an always-writable socket. This is a blocking acceptance gate for a curl-only raw backend. |
| T06 | Force repeated `WANT_*`/`CURLE_AGAIN`, partial successful writes and cancellation between them; hash/compare the final peer byte stream to prove no duplication, omission, mutation or extra newline. Check stable buffer/length requirements. |
| T07 | Split Schannel header/data/trailer and handshake output across many socket writes. Assert each record/token is generated once, retained until flushed, and output high-water limits apply. |
| T08 | Feed one-byte encrypted fragments, coalesced records and `SECBUFFER_EXTRA`; include success with zero decrypted application bytes and post-handshake control traffic. Assert no false EOF or data loss. |
| T09 | Leave plaintext/internal TLS data pending while the socket reports not readable; deliver replacement-work fixtures before the pre-submit decision. Partial/control-only buffered records cannot cause a busy loop. |
| T10 | Fragment/coalesce synthetic newline JSON messages at every byte boundary. Exact inbound cap is accepted; cap-plus-one/no-newline input is rejected without allocation growth or bypassing existing parsing/correlation. |
| T11 | Interrupt a TLS write after internal processing but before a success result, and after full local consumption but before a correlated reply. Both retain conservative unknown-outcome evidence; only “never attempted” is definitely unsent. No automatic share replay. |
| T12 | Clean `close_notify`, TCP EOF without alert, truncated JSON/TLS records, fatal alert and reset produce distinct terminal states as supported. Pending requests are not accepted by closure; stale jobs/buffers are invalidated on reconnect. |
| T13 | Apply the complete fallback table with consent absent/present. Every certificate/revocation/local-trust failure and unclassified TLS error remains ineligible for plaintext. A later timeout does not clear the security-failure latch or select a plaintext failover entry. |
| T14 | Consent for one endpoint/preset/revision cannot authorize another. Parse explicit schemes, bare addresses, bracketed IPv6, invalid ports, userinfo and malformed authorities without silently changing transport or port. |
| T15 | Enter donation from a plaintext-consented user lane. Donation remains independently TLS-required; an unverified donation route defers its window and resumes user work. No account/address/worker, certificate payload or private configuration appears in output. |
| T16 | HTTPS callback sizes of zero, one byte, exact body limit, limit-plus-one and simulated multiplication/size overflow; abort before append on excess and publish no partial snapshot. Allocation failure stays within the C callback boundary. |
| T17 | Missing/false `Content-Length`, chunked bodies, excessive complete header lines, interim responses and trailers; enforce independent cumulative limits. Measure library-buffer overhead separately from accepted-body/header caps. |
| T18 | An unexpected gzip response under identity-only policy is rejected. If compression is later enabled, a small compressed expansion bomb hits the decoded cap without excessive allocation or compute time. |
| T19 | Return 3xx to HTTP, another HTTPS origin, loopback and a local-file scheme; no follow-up request occurs. Reject non-allowlisted initial targets and remote feed-provided URLs before resolution. Script mixed public/private IPv4/IPv6 answers and answer changes; the production destination check cannot be bypassed by address selection or a second resolution. |
| T20 | Slow/trickling DNS/connect/TLS/header/body scripts cannot exceed total budgets. Cancel during each stage. Feed workers never block mining/protocol threads; queued requests/connections remain capped. |
| T21 | HTTP success containing malformed JSON, invalid UTF-8 where required, duplicate keys, excessive depth/nodes/strings, missing instrument/unit fields, invalid numbers or provider error fields fails schema validation. No invalid value becomes zero or a fresh observation. |
| T22 | Fixed-clock fresh/stale/cache transitions, capped retries, deterministic jitter seed, throttle response and shutdown; preserve old good snapshots with stale labels and stop when cancelled. |
| T23 | Set synthetic proxy/credential environment variables and a test credential file; local peer/proxy capture proves the public client sends neither credentials nor cookies and makes no unexpected proxy connection. A client-certificate request does not trigger automatic identity disclosure. |
| T24 | For curl connect-only, local peer observes TLS but no HTTP request/ALPN advertisement for HTTP; verify handle lifetime, partial I/O, buffered receive, and cleanup with the selected backend. Removing an easy handle is not used while raw I/O remains active. |
| T25 | Clean-machine Windows/Linux package tests with missing/wrong-architecture/mismatched-feature libraries, and harmless decoy DLLs in the working directory/search path. Load only approved components or fail closed; never fetch replacements at runtime. Test static and shared builds actually intended for release. |
| T26 | Concurrent stop/reconnect/lane-switch/error cleanup under sanitizers where available; no use-after-free, context races, stale request reuse, resource leak or secret-bearing diagnostic text. Preserve CPU-only offline test execution. |

The unit suite should assert operation/event counts as well as outcomes, so “eventually timed out” cannot hide a tight spin. OS-native trust/revocation behavior and third-party TLS allocations require isolated integration tests in addition to mocks; fake backends alone do not qualify production security.

## 11. Explicit remaining capability gaps

* **All three pools:** no live endpoint/certificate/SNI/chain, TLS-version or Stratum-over-TLS interoperability was verified. Documentation-level TLS candidates must not be labeled live-tested.
* **F2Pool and donation:** a Pearl-specific verified TLS endpoint was not established. No public default-TLS release claim may omit this blocker or silently resolve it by disabling verification/plaintext donation.
* **One-libcurl raw layer:** opposite-direction retry scheduling and buffered readiness under its chosen Schannel/OpenSSL builds remain to be demonstrated without private API dependence or polling spin.
* **Trust/revocation:** exact supported OS/distribution baselines, CA renewal/bundle policy, offline revocation behavior and cross-backend guarantees need a product decision. Certificate-verification failures must remain fatal regardless of that choice.
* **Feeds:** no PRL instrument/provider mapping, data license/access entitlement, provider uptime/freshness guarantee or live schema response was established. Bounded transport does not supply missing market/network semantics.
* **Distribution:** exact patched dependencies, final optional dependency graph, notices, CRT/provider artifacts, clean-runner behavior and size/performance measurements have not been produced. This document changes no code, packaging or existing license inventory.

## Sources

Repository links below identify implementation evidence and relevant line ranges as inspected; they are not claims of stable line numbering after future edits. External source-code citations pin the revisions actually inspected; those development revisions are **evidence, not recommended release pins**. Other external links are official live documentation retrieved on the date above.

[^repo-build]: [`CMakeLists.txt`](../../CMakeLists.txt), lines 18–42 and 72–80: CPU-only profile, C++17 and production source/link setup.
[^repo-transport]: [`src/core/pearl_pool_transport.h`](../../src/core/pearl_pool_transport.h), lines 22–76 and 87–185: transport contract, synchronous address/connect, readiness, socket I/O and blocking-mode changes.
[^repo-pool]: [`src/core/pearl_pool.h`](../../src/core/pearl_pool.h), `intakeBeforeSubmit` (lines 2226–2309), `sendLineUntil` (2721–2757), `readLineUntil` (2760–2818), and connection invalidation (2700–2718).
[^repo-config]: [`src/product/config.h`](../../src/product/config.h), `PoolEntry`, `poolPresets` and `parsePoolSpec` (lines 37–52 and 115–187).
[^repo-fee]: [`src/product/fee.h`](../../src/product/fee.h), independent compiled pool routing (lines 41–49); [`src/product/fee_session.h`](../../src/product/fee_session.h), `runFeeWindow` (lines 155–221), including explicit destination overrides and return-to-user/failure handling. Account/destination values are deliberately not reproduced.
[^schannel-init]: Microsoft, [InitializeSecurityContext (Schannel)](https://learn.microsoft.com/en-us/windows/win32/secauthn/initializesecuritycontext--schannel), target name, context flags, token ownership, return statuses and threading remarks.
[^schannel-encrypt]: Microsoft, [EncryptMessage (Schannel)](https://learn.microsoft.com/en-us/windows/win32/secauthn/encryptmessage--schannel), `SECPKG_ATTR_STREAM_SIZES`, maximum message size and record buffers.
[^schannel-decrypt]: Microsoft, [DecryptMessage (Schannel)](https://learn.microsoft.com/en-us/windows/win32/secauthn/decryptmessage--schannel), return values/remarks, post-handshake processing, empty output and OS library requirements.
[^schannel-extra]: Microsoft, [Extra Buffers Returned by Schannel](https://learn.microsoft.com/en-us/windows/win32/secauthn/extra-buffers-returned-by-schannel), unprocessed input and `SECBUFFER_EXTRA` ownership.
[^schannel-creds]: Microsoft, [SCH_CREDENTIALS](https://learn.microsoft.com/en-us/windows/win32/api/schannel/ns-schannel-sch_credentials), automatic/manual validation, no-default-credentials/name-check flags, TLS restrictions and minimum OS.
[^schannel-parameters]: Microsoft, [TLS_PARAMETERS](https://learn.microsoft.com/en-us/windows/win32/api/schannel/ns-schannel-tls_parameters), `grbitDisabledProtocols` and restriction semantics.
[^schannel-protocols]: Microsoft, [Protocols in TLS/SSL (Schannel SSP)](https://learn.microsoft.com/en-us/windows/win32/secauthn/protocols-in-tls-ssl--schannel-ssp-), OS/version table and TLS 1.3 support warning.
[^schannel-source]: curl upstream, [`lib/vtls/schannel.c` at `705b02e4160bead0f274791bc446fae5d2b2b717`](https://github.com/curl/curl/blob/705b02e4160bead0f274791bc446fae5d2b2b717/lib/vtls/schannel.c), credential flags and SNI target passed to `InitializeSecurityContext`.
[^openssl-support]: OpenSSL, [Release Strategy](https://www.openssl.org/policies/releasestrat.html), supported versions, LTS end dates and ABI policy.
[^openssl-write]: OpenSSL, [`SSL_write` documentation at `867020fb463eedb5a04d05dc769081c19d63fbf6`](https://github.com/openssl/openssl/blob/867020fb463eedb5a04d05dc769081c19d63fbf6/doc/man3/SSL_write.pod), NOTES, WARNINGS and RETURN VALUES; [published 3.5 manual](https://docs.openssl.org/3.5/man3/SSL_write/).
[^openssl-error]: OpenSSL, [`SSL_get_error` at the inspected 3.5 revision](https://github.com/openssl/openssl/blob/867020fb463eedb5a04d05dc769081c19d63fbf6/doc/man3/SSL_get_error.pod), same-thread/error-queue requirements, opposite-direction retries, EOF and fatal-error handling.
[^openssl-pending]: OpenSSL, [`SSL_pending` / `SSL_has_pending`](https://github.com/openssl/openssl/blob/867020fb463eedb5a04d05dc769081c19d63fbf6/doc/man3/SSL_pending.pod), processed versus unprocessed buffered data and false-positive pending hints.
[^openssl-host]: OpenSSL, [`SSL_set1_host`](https://github.com/openssl/openssl/blob/867020fb463eedb5a04d05dc769081c19d63fbf6/doc/man3/SSL_set1_host.pod), expected DNS/IP identity and separate SNI recommendation.
[^openssl-trust]: OpenSSL, [`SSL_CTX_load_verify_locations` and default paths](https://github.com/openssl/openssl/blob/867020fb463eedb5a04d05dc769081c19d63fbf6/doc/man3/SSL_CTX_load_verify_locations.pod), default trust sources, environment overrides and missing-location success semantics.
[^openssl-verify]: OpenSSL, [`SSL_CTX_set_verify`](https://github.com/openssl/openssl/blob/867020fb463eedb5a04d05dc769081c19d63fbf6/doc/man3/SSL_CTX_set_verify.pod), client `SSL_VERIFY_PEER`, default `SSL_VERIFY_NONE` and callback behavior.
[^openssl-version]: OpenSSL, [SSL_CTX_set_min_proto_version](https://docs.openssl.org/3.5/man3/SSL_CTX_set_min_proto_version/), minimum/maximum protocol API. Protocol-floor selection is a project policy.
[^curl-connect]: curl, [CURLOPT_CONNECT_ONLY](https://curl.se/libcurl/c/CURLOPT_CONNECT_ONLY.html), value 1, no transfer, reuse restrictions and multi-handle lifetime.
[^curl-send]: curl, [curl_easy_send](https://curl.se/libcurl/c/curl_easy_send.html), arbitrary protocols over TLS, nonblocking retries, buffer requirements and partial sends.
[^curl-recv]: curl, [curl_easy_recv](https://curl.se/libcurl/c/curl_easy_recv.html), cached data, retry behavior and zero-byte closure.
[^curl-socket]: curl, [CURLINFO_ACTIVESOCKET](https://curl.se/libcurl/c/CURLINFO_ACTIVESOCKET.html), active socket availability and library ownership/cleanup.
[^curl-openssl-source]: curl upstream, [`lib/vtls/openssl.c` at the inspected revision](https://github.com/curl/curl/blob/705b02e4160bead0f274791bc446fae5d2b2b717/lib/vtls/openssl.c), TLS send/receive handling of `SSL_ERROR_WANT_READ`/`WRITE`, `connssl->io_need` and `CURLE_AGAIN`.
[^curl-easy-source]: curl upstream, [`include/curl/easy.h`](https://github.com/curl/curl/blob/705b02e4160bead0f274791bc446fae5d2b2b717/include/curl/easy.h) and [`lib/easy.c`](https://github.com/curl/curl/blob/705b02e4160bead0f274791bc446fae5d2b2b717/lib/easy.c), public raw-I/O signatures and implementations at the inspected revision. The missing public direction result is a scoped API observation, not a claim about all future releases.
[^curl-certs]: curl, [TLS Certificate Verification](https://curl.se/docs/sslcerts.html), native versus file CA sources, Schannel CRL behavior and Windows native-store limitations for non-native backends.
[^curl-verifypeer]: curl, [CURLOPT_SSL_VERIFYPEER](https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html), chain verification, independent name verification and insecure-mode warning.
[^curl-verifyhost]: curl, [CURLOPT_SSL_VERIFYHOST](https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYHOST.html), `2L`, URL name and IP SAN rules.
[^curl-version]: curl, [CURLOPT_SSLVERSION](https://curl.se/libcurl/c/CURLOPT_SSLVERSION.html), minimum versus maximum settings and version-dependent defaults.
[^curl-ssloptions]: curl, [CURLOPT_SSL_OPTIONS](https://curl.se/libcurl/c/CURLOPT_SSL_OPTIONS.html), revocation overrides, native CA support, automatic client-certificate and early-data options.
[^curl-alpn]: curl, [CURLOPT_SSL_ENABLE_ALPN](https://curl.se/libcurl/c/CURLOPT_SSL_ENABLE_ALPN.html), ALPN enable/disable control.
[^curl-protocols]: curl, [CURLOPT_PROTOCOLS_STR](https://curl.se/libcurl/c/CURLOPT_PROTOCOLS_STR.html), scheme allowlisting, defaults and unsupported-feature returns.
[^curl-timeouts]: curl, [CURLOPT_CONNECTTIMEOUT_MS](https://curl.se/libcurl/c/CURLOPT_CONNECTTIMEOUT_MS.html), [CURLOPT_TIMEOUT_MS](https://curl.se/libcurl/c/CURLOPT_TIMEOUT_MS.html), and [CURLOPT_NOSIGNAL](https://curl.se/libcurl/c/CURLOPT_NOSIGNAL.html), connection/total deadlines and synchronous-resolver limitation.
[^curl-proxy]: curl, [CURLOPT_PROXY](https://curl.se/libcurl/c/CURLOPT_PROXY.html), ambient proxy environment variables and the empty-string override.
[^curl-write]: curl, [CURLOPT_WRITEFUNCTION](https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html), byte lengths, non-NUL-terminated chunks and abort semantics.
[^curl-header]: curl, [CURLOPT_HEADERFUNCTION](https://curl.se/libcurl/c/CURLOPT_HEADERFUNCTION.html), complete header lines, all response blocks/trailers and `CURL_MAX_HTTP_HEADER`.
[^curl-maxfile]: curl, [CURLOPT_MAXFILESIZE_LARGE](https://curl.se/libcurl/c/CURLOPT_MAXFILESIZE_LARGE.html), in-progress enforcement history and decompression enforcement added in 8.20.0.
[^curl-encoding]: curl, [CURLOPT_ACCEPT_ENCODING](https://curl.se/libcurl/c/CURLOPT_ACCEPT_ENCODING.html), decoding behavior, unexpected encoding, compressed lengths and expansion warning.
[^curl-libs]: curl, [Dependencies](https://curl.se/docs/libs.html), feature-dependent external libraries. The actual release graph must be inventoried rather than inferred from this illustrative list.
[^winhttp-options]: Microsoft, [WinHTTP Option Flags](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags), redirect policy, autologon, TLS protocol and certificate-ignore options.
[^winhttp-read]: Microsoft, [WinHttpReadData](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpreaddata), bounded reads, async buffer/output-parameter lifetime rules and platform DLL/import-library requirements.
[^kraken-ticker]: Kraken, [Get Ticker Information](https://docs.kraken.com/api-reference/market-data/get-ticker-information), public GET example, `pair` selection, `application/json` response and error/result schema.
[^kryptex-ports]: Kryptex Pool, [Pearl Miners Overview](https://pool.kryptex.com/articles/pearl-miners-en), regional server table and explicit SSL port 8048 statement.
[^kryptex-pool]: Kryptex Pool, [Pearl Mining Pool](https://pool.kryptex.com/prl), SSL-ready miner example on port 8048. No wallet examples or live statistics are reproduced.
[^hero-pool]: HeroMiners, [Pearl pool](https://pearl.herominers.com/), regional server list and TLS capability description.
[^hero-tls]: HeroMiners, [The miner settings that actually matter](https://herominers.com/help/main-miner-settings/), “TCP or TLS” section stating same-port support.
[^f2-tutorial]: F2Pool-operated help center, [How to mine Pearl](https://f2pool.zendesk.com/hc/en-us/articles/59248379419417-How-to-mine-Pearl), port 5500, plaintext examples and certificate-disabling example. Those examples are evidence only, not commands to execute.
[^f2-launch]: F2Pool-operated help center, [Pearl (PRL) pool launched](https://f2pool.zendesk.com/hc/en-us/articles/59741375087385-Pearl-PRL-pool-launched), published `stratum+tcp` Pearl endpoint. No financial or live operating figures are reproduced.
[^curl-license]: curl upstream, [`COPYING` at the inspected revision](https://github.com/curl/curl/blob/705b02e4160bead0f274791bc446fae5d2b2b717/COPYING), permission and notice requirements.
[^openssl-license]: OpenSSL upstream, [`LICENSE.txt` at the inspected revision](https://github.com/openssl/openssl/blob/867020fb463eedb5a04d05dc769081c19d63fbf6/LICENSE.txt), Apache-2.0 sections 3–4.
[^dll-security]: Microsoft, [Dynamic-Link Library Security](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-security), restricted search flags/directories and preloading risks.
[^dll-order]: Microsoft, [Dynamic-link library search order](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order), dependent-DLL resolution even when the parent is loaded by full path.
