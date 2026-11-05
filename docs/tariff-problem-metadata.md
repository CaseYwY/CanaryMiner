# Display-neutral tariff refusal metadata (P-13az)

M-I18N/M-UI; R-I18N-001/002; T-I18N/T-CONFIG/T-PRIVACY; D-151.

**Data-owner seam only, not translated native UI.** `TariffProblemKind` describes the existing rejection site independently
from the original path/message. Ordered machine choices and an optional typed electricity-model status carry their own
data, never fields recovered by parsing an English sentence. All legacy path/message bytes, predicate/validation priority,
output preservation, JSON/parser/store contracts and native English refusal rendering remain unchanged. No UI catalog,
typed terminal renderer, ICU, ambient locale or new dependency is imported into the model.

The default kind is Unknown for legacy callers or an empty/cleared problem. It does not mean success, validity, permission
or an observed utility state. Choice payload is present only for Choice, preserving original machine spellings/order;
model status is present only for ModelStatus. The old messages remain compatibility originals and are not replaced by
kind names or newly inferred source facts. Successful validation clears the complete diagnostic, including payloads.

Classifications cover missing/schema/duplicate-field, printable-label, bounded-integer/canonical-decimal/choice/collection,
missing-output/size/schema-version/acknowledgement/confirmation/currency/offset/date/quantity/arithmetic/model/publication/
JSON rejection sites. Existing DuplicateField and ArithmeticCapacity defensive sites remain; current exact-key membership
and bounded decimal inputs do not reach those two sites, so no runtime fault-injection coverage is claimed. The metadata
does not change supported schemas, missing-versus-zero, clocks, provider rules, confirmation freshness or admission.

No private data is newly disclosed or logged, and no helper/store/device action, receipt, supplier approval, automatic
reset, billing/optimizer/runtime activation or mining restart is added. Raw store/callback errors and later translated
reason/envelope binding remain separate. Saving still affects only the offline declaration/opening.

Fixtures exercise reachable classifications, exact ordered choices, typed model status, stale-payload clearing, legacy
Unknown defaults, parse/null-output refusals and unchanged output on refusal. An independent fixed-`3fb30d7` full-validator
comparison runs before/after with pinned public example/mutation vectors: **1328 synthetic cases / 1303 refusals** preserve
return value, rejection priority/path/message and explicitly checked decoded output fields. This is not an all-field decoded
plan oracle or supplier qualification. Existing fixed review-data **324 pairs / 2318 rows** and native **108160 frames /
33800 contextual transitions/resulting frames/active-draft-review serializations** remain independently pinned; no predecessor
or vectors are refreshed. Actual model/store/CLI/PTY evidence and exact platform/sanitizer/privacy results belong in the
[execution ledger](architecture/execution-plan.md).

Translated tariff diagnostic/reason binding, legacy/raw/other panels/dashboard/log domains, complete effective typed native/
data-owner/deployment, human review and physical terminal/font/shaping/accessibility remain open. Actual utility/rate/power/
safety/game/GPU/TLS/financial sources, global quarantine, physical durability and production release remain unqualified.
