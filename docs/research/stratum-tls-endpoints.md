# Pearl Stratum TLS endpoint evidence

**Reviewed:** 2026-11-04. **Scope:** published endpoint evidence for CanaryMiner's existing Kryptex, HeroMiners and F2Pool presets, including the independently configured developer-donation route. This is documentation research, not an implemented migration or live interoperability qualification.

**Public-document refresh, 2026-11-12:** the official Kryptex overview still specifies same-host SSL port 8048; its general tutorial still contains inconsistent 7048 SSL examples. HeroMiners' current help still documents same-port TCP/TLS and gives `de.pearl.herominers.com:1200` as a Pearl example, but the dynamic east-US regional row was not independently retrieved in this refresh. F2Pool's PRL information table still lists TCP 5500, and its Chinese tutorial still labels the SSL 5500 example invalid. No valid donation TLS tuple was established and no mining endpoint was contacted. Sources: [Kryptex overview](https://pool.kryptex.com/articles/pearl-miners-en), [HeroMiners settings](https://herominers.com/help/main-miner-settings/), [HeroMiners proxy guide](https://herominers.com/help/ssl-mining-proxy/), [F2Pool table](https://f2pool.zendesk.com/hc/en-us/articles/360058887912-Mining-pools-info-payout-schemes-and-thresholds-at-f2pool), [F2Pool Chinese PRL tutorial](https://f2pool.zendesk.com/hc/zh-cn/articles/59248379419417-PRL-Pearl-%E6%8C%96%E7%9F%BF%E6%95%99%E7%A8%8B).

**Later native handshake check, 2026-11-13:** Windows Schannel and Linux OpenSSL verified the documented Kryptex US TLS endpoint with system trust and no application bytes. See the exact [P-12j validation scope](../pool-tls-fallback.md#review-and-validation). The documentation-only and no-contact statements below describe the original research; this later check does not establish authentication, proof acceptance or a donation TLS endpoint.

Controlling project decisions are **D-13/D-18** in [decisions](../architecture/decisions.md) and **R-TLS-001–004** in [requirements](../architecture/requirements.md). TLS backend selection, software policy and packaging remain separate work. This note refreshes the endpoint evidence in [secure transport backends, §7](secure-transport-backends.md#7-pearl-preset-and-donation-endpoint-evidence); it does not adopt that note's backend recommendation.

## 1. Result and research boundary

* **Kryptex:** published TLS candidate for the existing regional preset is `stratum+ssl://prl-us.kryptex.network:8048`; the documented TCP port is `7048`.[^kryptex-miners][^kryptex-howto]
* **HeroMiners:** published TLS candidate is `stratum+ssl://us2.pearl.herominers.com:1200`; HeroMiners explicitly documents TCP and TLS on the same Stratum port.[^hero-pool][^hero-settings][^hero-proxy]
* **F2Pool:** documented Pearl endpoint is `stratum+tcp://pearl.f2pool.com:5500`. **No valid Pearl-specific TLS endpoint was established.** In particular, the Chinese tutorial explicitly labels its SSL scheme examples on port `5500` as examples, **not valid SSL addresses**.[^f2-tutorial][^f2-info][^f2-zh]
* **Donation:** the compiled route uses that same F2Pool host/port. It consequently has **no documentation-qualified TLS migration target** in this review. A documented user-pool TLS endpoint is not authorization to redirect donation to that pool.[^repo-fee]

Only public documentation was retrieved. Agent-reach's Exa search located pages; its Jina reader route supplied the page bodies. Search snippets alone were not accepted as endpoint evidence. All external findings below cite pool-operated pages, including F2Pool's official help center linked from its homepage.[^f2-home] These are mutable documents as retrieved on the review date, not a guarantee of origin freshness or continuing availability.

**No mining endpoint was contacted:** no pool DNS/connect/TLS test, port guess/scan, mining request, login, share submission, account lookup or support request. No personal configuration, account, wallet, telemetry or hardware was accessed. Existing repository source was read only. Public documentation may contain live statistics; none are used here as measurements or qualification evidence.

## 2. Coin and mainnet identity

The intended asset is **Pearl Network's native PRL, using PearlHash, on mainnet**—not another asset sharing a ticker, a test network or a merged-mining asset. The identity bridge is documentation-level, not inferred from a hostname alone:

| Pool | First-party identity evidence | Limit |
| --- | --- | --- |
| Kryptex | Its Pearl page names `PRL` and `PearlHash`; its About PRL page links the Pearl Research Labs source and explicitly labelled **Pearl Mainnet Explorer**, with `network=mainnet`.[^kryptex-pool][^kryptex-about] | Supports the intended preset/network mapping; no genesis or live job identity was checked. |
| HeroMiners | Its Pearl page describes PRL as the native asset of Pearl Network, links Pearl Research Labs/project resources, lists `pearlhash`, and discusses a Pearl **mainnet** fork while saying the pool address/port remain unchanged.[^hero-pool] | No Stratum negotiation, chain-state or proof-format compatibility was checked. |
| F2Pool | Its tutorial identifies Pearl's native PRL and PearlHash and links the project. The Chinese tutorial's final explorer link explicitly includes `network=mainnet`.[^f2-tutorial][^f2-zh] | Establishes the documented coin/network association, not live TLS support or account payout ownership. |

The repository defaults `PoolEntry.network` to `mainnet`, permits only mainnet for live use, and explicitly selects mainnet for donation. Endpoint migration must retain that identity and existing target/protocol semantics.[^repo-config][^repo-fee-session]

## 3. Endpoint register for the existing presets

**“Published TLS candidate” does not mean “certificate verified” or “works in CanaryMiner.”** The original source inspection found `stratum+ssl://` refused. Later [P-12h/j](../pool-tls-fallback.md) enables explicit user-pool URLs within its documented local-build scope; this does not upgrade any candidate's certificate or protocol qualification.[^repo-config]

| Preset / route | Documented plaintext endpoint | Published TLS endpoint | Required TLS SNI and certificate reference name | Qualification status |
| --- | --- | --- | --- | --- |
| `kryptex` — North America | `stratum+tcp://prl-us.kryptex.network:7048`[^kryptex-howto] | `stratum+ssl://prl-us.kryptex.network:8048`[^kryptex-miners] | `prl-us.kryptex.network` | Host plus SSL port explicitly documented; certificate, SNI behavior and Stratum-over-TLS untested. |
| `herominers` — east US | `stratum+tcp://us2.pearl.herominers.com:1200`[^hero-pool][^hero-settings] | `stratum+ssl://us2.pearl.herominers.com:1200`[^hero-pool][^hero-settings][^hero-proxy] | `us2.pearl.herominers.com` | Regional host/port plus explicit provider-wide same-port TLS statement; certificate, SNI behavior and Stratum-over-TLS untested. |
| `f2pool` — also compiled donation pool | `stratum+tcp://pearl.f2pool.com:5500`[^f2-tutorial][^f2-info] | **Unavailable: no valid published Pearl TLS endpoint established.**[^f2-zh] | **Unavailable until an actual TLS endpoint is documented.** | Do not create a TLS route by replacing the scheme or deriving a port/hostname. |

The SNI/reference-name column is a **project verification requirement**, not an observation of certificate contents or a pool promise about SANs. For either published candidate, retain the exact regional hostname as the configured DNS SNI and certificate-verification identity. Do not substitute a resolved IP, reverse-DNS name, website hostname, parent domain or a name learned from an untrusted certificate. A valid chain without matching the configured name is insufficient under R-TLS-003.

This register deliberately covers the existing preset regions, not an exhaustive list of regional alternatives. A region change needs its own explicit documented mapping and review; these rows do not authorize automatic regional failover or transfer plaintext consent.

### Kryptex: explicit SSL port, with a documentation inconsistency

The **Pearl Miners Overview** lists `prl-us.kryptex.network:7048` in its regional server table and states **“For SSL, use port 8048 on the same hosts.”** Independently, the Pearl pool page displays a generated example using `stratum+ssl://prl.kryptex.network:8048`. Together these support the same-host `8048` TLS candidate for the current US preset.[^kryptex-miners][^kryptex-pool]

The general **How to mine Pearl** guide explicitly lists the US TCP URL on `7048`, but some miner-specific SSL instructions leave `7048` in place, including an `ssl://` example. That is a documentation inconsistency, **not sufficient evidence of TLS on 7048**. Use the explicit SSL-port statement and pool-page `8048` example for the candidate mapping; do not assume both ports accept TLS or probe them to resolve the discrepancy.[^kryptex-howto][^kryptex-miners][^kryptex-pool]

### HeroMiners: same port is explicit, authenticated TLS is not established

The Pearl page lists **North America - East (USA)** as `us2.pearl.herominers.com:1200`. The official settings guide says **“The same port serves both here: stratum+tcp:// for a plain connection, stratum+ssl:// for an encrypted one.”** Its SSL proxy guide strengthens the scope: **“Every HeroMiners stratum port accepts both plain and TLS connections”**, gives a Pearl regional endpoint on `1200`, and directs readers to each pool's regional list.[^hero-pool][^hero-settings][^hero-proxy]

This is affirmative documentation-level TLS support for the registered regional endpoint, not proof of a trusted certificate for `us2.pearl.herominers.com`. The proxy guide's sample explicitly sets `verifyChain = no`; **that sample is not an authenticated-TLS solution and must not be copied**. This review recommends neither a proxy deployment nor a verification bypass.[^hero-proxy]

### F2Pool: affirmative TCP evidence; invalid SSL examples must not qualify

The English tutorial, consolidated pool-information page, homepage PRL entry and original Pearl launch announcement publish `stratum+tcp://pearl.f2pool.com:5500`. The consolidated page expressly lists SSL addresses for other coins but supplies only that TCP URL in its PRL row. Those other-coin addresses provide no Pearl hostname/port mapping.[^f2-tutorial][^f2-info][^f2-home][^f2-launch]

The Chinese Pearl tutorial contains SSL scheme-substitution examples on `5500`, but explicitly qualifies them with **“该地址仅用作示例，非有效SSL地址”**—**“this address is only an example, not a valid SSL address.”** Some examples also use an insecure SSL scheme. The English tutorial includes `--no-cert-validation` in one miner command. Neither a clearly invalid example nor a certificate-disabling option establishes a usable, authenticated Pearl TLS endpoint.[^f2-zh][^f2-tutorial]

**Safe capability result:** Pearl TLS endpoint **unavailable / not established by the reviewed documentation**. This is not an assertion that F2Pool can never offer TLS. It is also not permission to try speculative SSL hostnames, other coins' ports, `5500` with a new scheme, custom trust exceptions or plaintext after a certificate failure.

The consolidated information page was successfully readable in this review; the retrieval limitation recorded in the older backend study is therefore **not** the basis for today's gap. The gap remains because the successfully read PRL material supplies no valid TLS endpoint.[^f2-info]

## 4. Developer-donation implications and conservative migration choices

### Existing source contract, without reproducing identifiers

[`src/product/fee.h`](../../src/product/fee.h) compiles an independent pool route of **`pearl.f2pool.com:5500`**, using the F2Pool preset. It defines a **99:1 productive-time policy**: a one-minute donation window per 100 productive minutes, randomized within the cycle; stopped/connecting/idle time is not credited. Unmet work is carried under the existing one-window debt cap; a pool failure defers the window rather than blocking user mining. Destination and login identifiers are deliberately not reproduced here.[^repo-fee]

`runFeeWindow` copies run options but explicitly replaces the lane, configured destination, worker, pool host/port, pool account, proof-compression choice and mainnet target policy. Consequently, future transport fields must be explicitly resolved for this independent route, not inherited from a user's Kryptex/HeroMiners TLS choice or plaintext consent. F2Pool documents account-name mining and account-managed payout settings; this public research does not authenticate the compiled account's payout configuration or relationship to the configured destination.[^repo-fee-session][^f2-tutorial]

### Options for the parent migration decision

These are endpoint-driven recommendations, **not authorization or implementation of a donation-policy change**:

1. **Most conservative full migration: hold an all-lanes verified-TLS release claim** until F2Pool publishes a valid Pearl TLS host/port and a separately authorized qualification establishes strict certificate/name verification and protocol compatibility. A later transport-only change must retain the existing compiled destination/account/worker, donation amount, productive-time/debt accounting and F2Pool protocol/compression behavior. A different pool is not an equivalent destination just because it mines PRL.
2. **Stage user-pool migration independently:** qualify the documented Kryptex/HeroMiners TLS candidates without claiming donation TLS is solved. If an approved strict-TLS mode cannot use the donation route, report that route unavailable and use the existing failure/defer behavior, preserving owed work under its unchanged cap and returning to user work. Do not mark unmined donation time complete, silently disable donation, erase debt, increase the percentage, or send it elsewhere. Adoption of this staged behavior remains the parent's policy/integration decision.[^repo-fee][^repo-fee-session]
3. **Intentional legacy compatibility, only if separately approved:** the documented F2Pool TCP endpoint can remain an explicitly warned plaintext-only route, not a discovered TLS fallback. Any such approval must be donation-lane-specific; user-pool consent cannot authorize it. This preserves the endpoint/protocol but **does not provide TLS** and cannot satisfy an all-lanes verified-TLS claim. This research grants no consent and changes no default.

For all options, preserve donation destination and amount, Pearl protocol bytes/dialect and mainnet identity. Never redirect donation to the user's pool, use the user's account or convert an unqualified endpoint into “verified” by disabling certificate checks. Under D-13 and R-TLS-002/003, certificate/name/trust failures—including failures on a published candidate—must never trigger plaintext downgrade, alternate identity or trust fallback. Explicit strict SSL remains strict; absent documentation remains unavailable.

## 5. What remains unverified

For **all** candidates, this research establishes published documentation only. It does **not** establish DNS resolution, current listener reachability, SNI routing, certificate issuer/SAN/validity/chain/revocation, accepted TLS versions/ciphers, cancellation behavior, pool login success, compression negotiation, mining-job compatibility, accepted shares or payout destination. No certificate fingerprint or CA exception can be derived from these pages.

Endpoint admission should preserve separate states: **documented candidate**, **runtime certificate-verified connection**, and **protocol-qualified route**. None substitutes for the next. Recheck the exact official host/port documentation before a future authorized qualification; the present task authorizes no such connections. F2Pool/donation stays unavailable for TLS until affirmative Pearl-specific evidence exists. Backend/trust-store implementation and fallback classification are outside this endpoint study.

## Sources

All external citations are first-party pool documentation whose page bodies were read on 2026-11-04. Repository citations describe source inspected during this review; line numbers may move.

[^kryptex-miners]: Kryptex Pool, [Pearl Miners Overview](https://pool.kryptex.com/articles/pearl-miners-en), “Pearl pool stratum addresses”: regional table and explicit same-host SSL port `8048` statement.
[^kryptex-pool]: Kryptex Pool, [Pearl mining pool](https://pool.kryptex.com/prl), PRL/PearlHash identity, regional addresses, wallet-project link and generated `stratum+ssl` example on `8048`.
[^kryptex-howto]: Kryptex Pool, [How to mine Pearl (PRL)](https://pool.kryptex.com/articles/how-to-mine-pearl-en), “Quick-start mining settings” and miner-specific examples; explicit TCP regional URLs and inconsistent SSL instructions retaining `7048`.
[^kryptex-about]: Kryptex Pool, [About PRL](https://pool.kryptex.com/prl/about-coin), Pearl/PearlHash identity, Pearl Mainnet Explorer and official project/source links.
[^hero-pool]: HeroMiners, [Pearl pool](https://pearl.herominers.com/), “Start” regional address list, Pearl identity/project links, TLS feature description and 2026-08-11 mainnet announcement.
[^hero-settings]: HeroMiners, [The miner settings that actually matter](https://herominers.com/help/main-miner-settings/), “TCP or TLS”: explicit same-port support and scheme distinction.
[^hero-proxy]: HeroMiners, [Set up an SSL mining proxy](https://herominers.com/help/ssl-mining-proxy/), “Which host to connect to”: every Stratum port accepts plain/TLS, Pearl example and regional-list scope; stunnel sample disables chain verification and is not recommended here.
[^f2-tutorial]: F2Pool Help Center, [How to mine Pearl](https://f2pool.zendesk.com/hc/en-us/articles/59248379419417-How-to-mine-Pearl), Pearl/PRL identity, account-managed mining/payouts and TCP `5500` examples; one includes a certificate-disabling flag.
[^f2-zh]: F2Pool Help Center, [PRL（Pearl）挖矿教程](https://f2pool.zendesk.com/hc/zh-cn/articles/59248379419417-PRL-Pearl-%E6%8C%96%E7%9F%BF%E6%95%99%E7%A8%8B), §5: explicit “example only, not valid SSL address” qualifications; §7: project and `network=mainnet` explorer links.
[^f2-info]: F2Pool Help Center, [Mining pools info, payout schemes and thresholds at f2pool](https://f2pool.zendesk.com/hc/en-us/articles/360058887912-Mining-pools-info-payout-schemes-and-thresholds-at-f2pool), PRL row: only TCP `5500`; SSL addresses for other coins are coin-specific.
[^f2-home]: F2Pool, [public homepage](https://www.f2pool.com/), PRL PearlHash entry, TCP pool URL and official tutorial link. Incidental market/mining statistics were not used.
[^f2-launch]: F2Pool Help Center, [Pearl (PRL) pool launched](https://f2pool.zendesk.com/hc/en-us/articles/59741375087385-Pearl-PRL-pool-launched), published PRL TCP URL and coin identity. Historical payout-scheme statements are not used as current donation/accounting facts.
[^repo-config]: [`src/product/config.h`](../../src/product/config.h), `PoolEntry`, `poolPresets`, `parsePoolSpec`, `validNetwork` and `poolPresetSpeaksV2`: current regional presets, mainnet defaults, SSL refusal and protocol-dialect selection.
[^repo-fee]: [`src/product/fee.h`](../../src/product/fee.h), header contract, `CANARY_FEE_POOL_HOST/PORT/PRESET`, `FeePolicy`, `FeeScheduler::advance`, `startCycle` and `noteFeePoolFailure`: independent public pool route and existing donation accounting. Destination/account/worker values are intentionally omitted.
[^repo-fee-session]: [`src/product/fee_session.h`](../../src/product/fee_session.h), `FeeSession::runFeeWindow`: explicit donation-lane overrides, mainnet/target policy, proof compression, failure handling and return to user work.
