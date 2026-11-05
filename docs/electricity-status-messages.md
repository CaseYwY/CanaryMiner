# Human electricity arithmetic and coverage descriptions (P-13bb)

M-I18N/M-UI; R-I18N-001/002; T-I18N/T-UI/T-CONFIG/T-PRIVACY; D-153.

**English-only native source binding and independent typed drafts.** Eleven stable `electricity.status.*` literal keys in
sixteen packs (176 texts) describe declared arithmetic, partial/missing coverage, invalid/unsupported inputs, dynamic-price
gaps, conflicting/late rates, regressed clocks, bounded capacity and unknown status. These are human descriptions, not wire
enum names or evidence that their predicates hold. Non-English entries remain machine-assisted drafts, not human-reviewed
financial language or effective native UI. English explicitly retains not-supplier-certified/no-carry-forward limitations;
catalog parity cannot certify human semantic fidelity or financial/source truth.

The UI-only `electricity_status_source.h` explicitly maps the ten known `ElectricityStatus` values, never a parsed sentence.
Native tariff ModelStatus handling uses the typed optional payload only under the matching problem kind and only when the
English source is byte-identical to the original. Unsupported enums, missing payloads and changed originals remain raw;
the separate Unknown draft key does not convert an unsupported enum into a known fact. Non-model problems ignore stray
status payloads. Existing `electricityStatusName` stays untouched in the pure core as the legacy source API. Model/parser/
store and arithmetic headers import no UI catalog or bridge.

Native tariff refusal path/message/priority/focus, active/draft/review, raw source serialization and offline Save/stop remain
unchanged. No classification, description or source equality creates coverage, fills a price gap, approves a supplier rule,
extends a clock/freshness window or grants collection/mining/reset/billing/optimizer permission. Actual model/owner/source
contracts retain authority. A synthetic ModelStatus/Ok combination in the mapping fixture is not an actual admitted refusal
or a supplier-certified result.

`TerminalElectricityStatusMessageRequest` and four independent layout/wrap/paint wrappers are argument-free and receive no
model/clock/coverage/rate/source/private declaration or observation. Complete English ASCII/disabled recovery, requested/
rendered review, tone/source/control/cell provenance, independent wrapped rows and transactional refusal reuse the shared
pipeline. Ordinary native rendering selects only English and calls no typed renderer or ICU. No rendered status is parsed
back into enums, preferences, JSON, commands or permissions.

Fixtures check all 176 texts, exact English/key/review/RTL/no-parameter parity and every order/direction/width/plain/paint/
provenance/independent-row mode, with unchanged outputs for invalid key/language/tone/policy/row/null calls. Memory-only
known-enum mappings match the unchanged core strings, preserve changed/unsupported/missing/non-model raw cases and ignore
stray payload authority. A real pure-model invalid-opening call preserves typed refusal/output, and native review preserves
its exact path/message and active/draft/stop without Save access. These are synthetic model observations, not actual utility,
meter, price, power, coverage or supplier qualifications. Fixed-`3fb30d7` native comparison remains **108160 frames / 33800
contextual transitions/resulting frames/active-draft-review serializations**, empty save access and unchanged vectors/
predecessor; validation remains **1328 cases / 1303 refusals**. Actual store/model/CLI/PTY and exact platform/sanitizer/privacy
results are in the [execution ledger](architecture/execution-plan.md).

Raw diagnostic envelopes/legacy and other panels/dashboard/log/raw domains, complete effective typed native/data-owner/
deployment, human review and physical terminal/font/shaping/accessibility remain open. Actual utility/rate/power/safety/
game/GPU/TLS/financial sources, global quarantine, physical durability and production release remain unqualified.
