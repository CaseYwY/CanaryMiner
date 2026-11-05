# Private tariff change records and review envelopes (P-13ay)

M-I18N/M-UI; R-I18N-001/002; T-I18N/T-UI/T-CONFIG/T-PRIVACY; D-150.

**English-only native extraction and independent typed drafts.** Four stable `tariff.review.*` keys in sixteen packs
(64 templates) cover complete value-change and row-count-change lines, each with explicit nested-path versus root forms.
Root forms preserve old punctuation/leading space without inventing a nonempty invariant path. Non-English entries remain
machine-assisted drafts, not human-reviewed financial language or effective native UI.

The new display-neutral `TariffDeclarationChange` record distinguishes original `beforeJson`/`afterJson` from structural
`beforeRows`/`afterRows` counts and retains the original schema path. `tariffDeclarationChangeRecords` preserves the old
traversal, ordering, equality test, object/array/root handling and exact primitive serialization. It validates no declaration,
supplier or source fact. Model code imports no UI catalog. The legacy `tariffDeclarationChanges` API still emits the exact
old strings; no caller must parse them back to recover a path, count or JSON token.

Native review consumes the records directly and selects only exact English. Diff rows, complete unchanged-field rows,
headings/no-change/confirmation lines, offsets/wrapping/budgets, active/draft/review bytes and save/cancel/stop are unchanged.
There is no translation from rendered strings back into model data. The original parser/store/clock/decimal/source contracts
remain authoritative, including existing behavior for deliberately invalid developer tree shapes; this extraction does not
silently fix or admit those shapes. Private records and source review lines must not be logged or publicly exported.

Source-only one-pass English interpolation checks exact names but imposes no new value-content/size refusal on native
originals. It is not a terminal-safe renderer, JSON/path validator or disclosure boundary. Independent typed requests instead
require nonempty invariant path within 512 original/recovery bytes, nonempty invariant before/after JSON-display originals
within 1024 each, or nonempty non-invariant human atomic-LTR before/after row-count display within 256 each. The key fixes
which role is allowed; root templates take no path. Shared parameter/UTF-8/control/recovery/policy checks refuse all four
wrappers transactionally, including ASCII/disabled recovery. Invariant/atomic spans stay whole or omitted under clipping;
failed wraps publish nothing. These checks do not parse or validate JSON, numeric counts, path existence or financial truth.

Independent callers retain their own private-output controls. This batch adds no log/public-diagnostic copy, helper/store/
device action, Save receipt, supplier approval, automatic reset, billing/optimizer/runtime activation or mining restart.
Ordinary native rendering calls no typed renderer or ICU. Raw original validator/store errors remain unchanged and separate.

Fixtures check all 64 templates, exact English/key/review/RTL/parameter/arrow parity, one-pass/source-only raw preservation,
every order/direction/width/plain/paint/provenance/independent-row mode and per-argument role/nonempty/atomic/UTF-8/control/
original/recovery refusal. Direct records preserve root/nested values, distinct count fields, additions/removals, original
tokens and legacy lines across developer tree shapes; native review preserves diff plus complete-source rows without Save.
An independent fixed-`3fb30d7` serializer/traversal comparison covers **324 ordered shape pairs / 2318 raw review rows** before
and after the data refactor, including structured reconstruction afterward. The separate existing fixed-native comparison
retains **108160 frames / 33800 contextual transitions, resulting frames and active/draft/review serializations**, empty save
access and unchanged pinned vectors/predecessor. Actual store/model/CLI/PTY evidence remains separate; exact results are in
the [execution ledger](architecture/execution-plan.md).

Tariff raw errors/legacy APIs and other panels/dashboard/log/raw domains, complete effective typed native/data-owner/
deployment, human review and physical terminal/font/shaping/accessibility remain open. Actual utility/rate/power/safety/
game/GPU/TLS/financial sources, global quarantine, physical durability and production release remain unqualified.

[P-13az](tariff-problem-metadata.md) subsequently adds display-neutral refusal classifications/payloads without changing
these historical review results, legacy path/message bytes or model/UI import boundaries. Reasons remain untranslated.
