# Decision register

Settled product decisions are distinct from unresolved implementation choices. This register preserves the reason and prevents later agents from accidentally reverting the approved valuation or reusing private prototype assumptions.

| ID | State | Decision and reason |
|---|---|---|
| D-01 | Accepted | Work in a clean upstream fork; keep changes small and preserve upstream MIT attribution, device/kernel/donation/protocol semantics. Private deployment folders are not contribution sources. |
| D-02 | Accepted | Native terminal settings entered with S, plus CLI/config/idle settings. No separate GUI/browser runtime. Modern CLI interaction patterns are references, not branding/dependency choices. |
| D-03 | Accepted | Explicit session start and intentional-stop precedence. No implicit startup mining, driver reset/reboot, game killing or permission escalation. |
| D-04 | Accepted | Maximize estimated net per elapsed time under safety/game limits, using measured local curves, not raw speed or efficiency alone. |
| D-05 | Accepted | Accepted history uses rolling 24h/7d/30d/all-local coverage; donor shares stay separate. It is not token revenue. |
| D-06 | Accepted | Main price view combines last-1h equal-time average and latest quote; provider identity/freshness/coverage and quote currency remain explicit. |
| D-07 | Accepted — final | Main-panel actual token income is valued at the **latest** valid price. Optional per-credit recorded estimates may appear in Details only. Neither method proves a sale, actual cash receipts or current holdings. Earlier per-credit-as-main-default drafts are superseded. |
| D-08 | Accepted | Today means calendar actual credited-so-far plus remaining-time forecast in the selected timezone. Missing current-day facts cannot produce a fabricated complete forecast. |
| D-09 | Accepted | USD and USDT are not interchangeable. A USDT pair needs explicit valid conversion for USD display; missing conversion leaves USD unavailable. |
| D-10 | Accepted | No entered electricity plan means no net-profit row. Board-power estimated net is permitted with clear basis/exclusions; it is not whole-PC realized profit. |
| D-11 | Accepted | Build composable plan/state-aware tariffs with source/effective revisions. Do not infer a price from country/language, flatten genuine all-units bands or ignore shared-meter/demand/prepaid/export effects. |
| D-12 | Accepted | Game policy is independently off/pause/coexist; coexistence is bounded, authorized and experience-qualified. Launcher tests do not prove gameplay; prefer miner-side throttling. |
| D-13 | Accepted | Secure-by-default TLS preference, strict SSL, per-pool opt-in plaintext fallback only for allowed connectivity failures; certificate/name/trust failures never downgrade. |
| D-14 | Accepted | Windows/Linux core parity with explicit capability differences; currently disabled hardware tuning is not incidentally re-enabled. |
| D-15 | Accepted scope | First internationalization milestone covers the 16 locale IDs listed in terminal-experience.md, with review status, English fallback and Unicode/RTL support. It is not a claim that packs already exist. |
| D-16 | Accepted | Financial/config/game data stays local; exports/provider disclosure are controlled and public fixtures synthetic. Only authorized public contributor metadata is used. |
| D-17 | Accepted for local accepted history; credit schema still open | Optional system SQLite >=3.31 ABI, WAL/FULL transactions, exclusive writer, bounded async queue, 30-day detail plus permanent local totals and fail-closed recovery. See [storage ADR and alternatives](../accepted-history.md). Exact-decimal credit records/scope need their own schema review; this does not make accepted shares income. |
| D-18 | Open technical ADR | Select maintained TLS/HTTPS backends and trust stores with Windows/Linux packaging and verified pool/donation endpoints. No handwritten crypto or insecure workaround. |
| D-19 | Open technical ADR | Select timezone, exact-decimal and Unicode/grapheme/bidirectional support with licensing, versioning and deterministic tests. |
| D-20 | Open capability | Verify actual credit attribution/interval support, game frame metrics and whole-system/incremental measurements. Missing capability stays unavailable, not simulated evidence. |
| D-21 | Accepted conservative observation coverage | Polling cannot prove complete acknowledgement capture at worker exit. Persist uncertain shutdown tails and inter-session intervals, report latest recent gap, keep all-time uncertainty flags, and suppress rolling totals after a clock warning rather than repairing timestamps or clearing warnings implicitly. |

Any later change must add its rationale and affected requirements/tests here. Do not erase the previous decision or change a display label to conceal different semantics.
