# Offline-tariff editor guidance (P-13av)

M-I18N/M-UI; R-I18N-001/002; T-I18N/T-UI/T-CONFIG/T-PRIVACY; D-147.

**English-only native extraction and independent typed drafts.** Eleven stable `tariff.hint.*` literal keys in sixteen packs
(176 texts) cover tier/time-mode/clock-kind/date/optional-decimal/UTC-ms/opening/mask/fixed-offset/minute/default editor
guidance. Non-English entries remain machine-assisted drafts, not human-reviewed financial language or effective native UI.
Machine enum spellings, date grammar, null, explicit units/ranges and ordering constraints stay visible; token parity alone
does not establish a faithful human translation or validate a declaration.

Native `hint()` retains the original exact-name tests, substring tests, case sensitivity, priority and default. UTC-ms wins
over mask/minute collisions; mask wins over minute. Optional-decimal and opening pairs remain exact matches, not inferred
from translated labels or units. Field/schema identifiers and values are unchanged. Complete English frames, editor input,
contextual keys, rejection latch, focus/scroll/wrap/budgets, active/draft/review and save/cancel/stop remain unchanged.

Guidance is not validation: the original JSON/parser/model/clock/decimal/source contracts alone decide type, syntax, range,
timezone, null, endpoint exclusivity, register composition and opening eligibility. Describing explicit zero or missing
history does not create it. A hint matching a developer-only unknown field does not admit that field into the schema. This
batch adds no account access, automatic now/reset, supplier approval, billing/optimizer/runtime activation or mining restart.

The independent argument-free `TerminalTariffHintMessageRequest` and four layout/wrap/paint wrappers receive no selected
field, input, declaration, private path/source, amount, clock, opening or store reply. They reuse complete English ASCII/
disabled recovery, requested/rendered review, tone/source/control/cell provenance, independent wrapped rows and transactional
refusal. Ordinary native rendering selects English and calls no typed renderer or ICU. Model/parser/store and electricity/
civil arithmetic import no UI catalog; no translated hint is parsed into JSON, preferences or commands.

Fixtures check all 176 texts, exact English/key/review/RTL/no-parameter/protected-grammar parity and every order/direction/
width/plain/paint/provenance/independent-row mode. Invalid key/language/tone/policy/row/null calls preserve output. Native
memory-only vectors cover all eleven hints through actual editor rendering, exact paired names, substring overlaps, case
sensitivity and default/empty names, with unchanged active/draft/input and explicit cancel/stop. Synthetic unknown fields
are selection comparisons, not model admission; no Save callback is provided. Fixed-`3fb30d7` comparison retains **108160
frames / 33800 contextual transitions, resulting frames and active/draft/review serializations**, empty save access and pinned
example vectors. Actual store/model/CLI/PTY regressions remain separate; exact results are in the
[execution ledger](architecture/execution-plan.md).

Tariff dynamic/raw JSON and other panels/dashboard/log/raw domains, effective typed native/data-owner/deployment, human
review and physical terminal/font/shaping/accessibility remain open. Actual utility/rate/power/safety/game/GPU/TLS/financial
sources, global quarantine, physical durability and production release remain unqualified.

[P-13aw](tariff-context-messages.md) subsequently extracts editor/path/position envelopes without changing these historical
hint results, matching predicates or the distinction between source-only raw preservation and typed/schema validation.
