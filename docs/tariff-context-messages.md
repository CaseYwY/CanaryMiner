# Offline-tariff editor, path and position envelopes (P-13aw)

M-I18N/M-UI; R-I18N-001/002; T-I18N/T-UI/T-CONFIG/T-PRIVACY; D-148.

**English-only native extraction and independent typed drafts.** Ten stable `tariff.context.*` keys in sixteen packs
(160 templates) cover editor field/row/no-selection headings, editor input, draft-row removal, row names, array-row prompts,
schema paths/root declaration and complete field-position/count rows. Non-English entries remain machine-assisted drafts,
not human-reviewed financial language or effective native UI. Schema field/path/input originals are distinct from human
position/count display. The latter are not machine identifiers or automatically formatted/localized numeric facts.

Native rendering retains exact English, original zero-based row/removal indices, one-based nonempty field position and 0/0
empty table. Root path is a literal human description; nested paths preserve the original schema syntax. Selected-name
English bytes still reach the unchanged editor/type/read-only/hint predicates. Contextual Q/D/L/S, input/rejection latch,
review/save/cancel/stop, focus/scroll/wrap/budgets and active/draft/review serialization remain unchanged. Nothing is inferred
from a displayed count, label, private declaration path or input.

Source-only `tariffContextEnglish` checks exact parameter names and expands once without reinterpreting or sanitizing value
bytes. It deliberately imposes no new value-content/size refusal on native originals: developer-only invalid inputs, empty
names and overlong/hostile strings still produce the old source output, not silently blanked fields. This is not a general
terminal-safe renderer, schema validator or disclosure boundary, and does not make those developer states valid declarations.
Actual parser/store/editor bounds and the existing native wrapping remain authoritative and unchanged.

The independent typed request is stricter: schema field and editor input are invariant within 128 original/recovery bytes;
schema path is nonempty invariant within 512. Only input may be empty. Position/count must be nonempty non-invariant human
atomic-LTR within 256 original/recovery bytes. Shared exact-parameter/UTF-8/control/recovery/policy checks refuse all four
wrappers transactionally, including disabled-backend recovery. Invariant/atomic spans stay whole or omitted when clipped;
failed wraps publish nothing. These bounds neither validate syntax, range, selection, path existence nor numeric truth.

No JSON source value, private review diff or raw validator/store reply is localized by this batch. No helper/store/device
action, automatic now/reset, supplier approval, receipt, billing/runtime activation or mining restart is added. Ordinary
native rendering calls no typed renderer or ICU; model/parser/store/electricity/civil arithmetic import no UI catalog.
Rendered strings are not parsed into preferences, schema paths, commands, clock or model values.

Fixtures check all 160 templates, exact English/key/review/RTL/parameter/token parity, reordered/one-pass/empty-input/source-
only raw preservation, every order/direction/width/plain/paint/provenance/independent-row mode and per-argument role/nonempty/
atomic/UTF-8/control/original/recovery refusals. Native memory-only vectors cover root/nested path, array/field/removal indices,
empty tables, object/row/no-selection editing, braces and input bounds without Save access; deliberately unknown fields and
129-byte input are rendering comparisons, not admission. Fixed-`3fb30d7` comparison retains **108160 frames / 33800 contextual
transitions, resulting frames and active/draft/review serializations**, empty save access and pinned example vectors. Actual
store/model/CLI/PTY regressions remain separate; exact evidence is in the [execution ledger](architecture/execution-plan.md).

Tariff raw JSON/review and other panels/dashboard/log/raw domains, effective typed native/data-owner/deployment, human
review and physical terminal/font/shaping/accessibility remain open. Actual utility/rate/power/safety/game/GPU/TLS/financial
sources, global quarantine, physical durability and production release remain unqualified.

[P-13ax](tariff-value-messages.md) subsequently extracts complete browse rows around original JSON without changing these
historical context results, source-only raw preservation or the stricter independent typed display boundary.
