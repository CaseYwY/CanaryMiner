# Details/Log controls and financial presentation contract

This development batch extends native terminal settings. It does not fetch live market/reward data or calculate an electricity bill.

## Working controls

- **Details:** enable/disable the page, device identity/PCI bus, pool state, restart count and last-error detail. Global worker/safety state remains visible. A disabled page has no advertised D shortcut and cannot be opened with D; disabling the current page returns to Main.
- **Log:** enable/disable the page, structured severity threshold, independent inclusion of unclassified raw lines, latest 25/50/100/200 records, observed timestamps, UTC time format and follow/freeze. L is likewise disabled with the page.
- Raw worker text stays **unclassified**, even if it contains words such as “warning”. Severity filtering does not invent trusted structured metadata. The unclassified switch is independent of the structured threshold.
- Observed timestamps are added by the renderer. Embedded timestamps in raw worker text are not removed or reinterpreted.
- Follow shows new output; Up/PageUp holds scrolling and F resumes. Turning follow off freezes the current displayed snapshot; re-entering Log takes a new one. Background pipe reading and bounded collection continue.
- Changes remain drafts until review/save. Long change sets can be scrolled with arrows/PageUp/PageDown. Short terminals keep the selected editable field visible.

## Presentation file version and rollback

The writer uses [schema v2](../config/ui-preferences-v2.schema.json). It still reads [v1](../config/ui-preferences-v1.schema.json), applying documented defaults for new fields. Loading a file never upgrades it implicitly.

The first explicit save of a v1 file preserves a validated, immutable sibling `ui-preferences.json.v1.bak` before replacing the active file. A pre-existing invalid/non-regular backup blocks the upgrade; it is not overwritten or deleted. To use an older build, close active settings writers and restore the v1 backup deliberately. Mining config, provider credentials and financial/history records are not written into this preferences file.

## Financial contract, not a live integration

`financial_view.h` is a read-only, synthetic-input-tested presentation interface. It implements the approved rule:

- Actual token-credit quantities do not change on price updates.
- Main reference valuations use the latest valid token/USDT and USDT/USD quotes. USDT is not assumed equal to USD.
- Latest and 1h-average USD values are different fields; the average requires a validated, time-aligned source calculation.
- Partial/missing/stale values retain their status, not invented zeroes. Dollar values are production references, not sales, realized profit or current holdings.
- No configured electricity means **no net row**. A configured zero/negative rate is different from no plan. An estimated-net row requires a complete forecast, finite USD cost and a matched horizon, with explicit energy basis/exclusions.

The runtime `FinancialView.enabled` default remains false: **live prices, credit attribution, today's forecast inputs and tariff/energy calculation are not integrated yet**. Local [accepted-counter history](accepted-history.md) is now separately implemented; it never supplies financial facts. The renderer's floating-point fields are display projections, not an exact persistent accounting representation.

## Validation boundary

Native Windows/Linux unit tests exercise preferences, migration, D/L field/shortcut filtering, structured-versus-unclassified logs, freeze/follow and current-price valuation. POSIX PTY tests exercise the real supervisor over fake workers, including saved D/L disable and continued Main/Q behaviour. No real miner/GPU/game is launched by these tests.

The existing full pinned-toolchain/verifier/protocol/SASS/production-GPU/release gates remain unrun for this development batch. Do not distribute it as a new qualified miner release on the strength of host/formatting tests.

See [executed host validation and its limits](display-validation.md).
