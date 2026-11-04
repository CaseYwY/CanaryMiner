# Complete settings-review changes (P-13n)

M-I18N/M-UI; R-I18N-001/002; T-I18N/T-UI/T-CONFIG/T-PRIVACY; D-112.

**Source extraction and separate typed draft rendering, not effective native translation.** Sixteen stable
`settings.change.*` keys in sixteen source/draft packs cover the complete root `SettingsPanel::changes()` domain (256
texts). Each is a whole sentence/template with named `{before}` and `{after}` roles, rather than a translated label glued
to a universal punctuation rule. All non-English packs remain machine-assisted drafts. Native review explicitly calls
English-only helpers and preserves its old change order, strings, scrolling, save/cancel/conflict behavior and stop routes.

The roles are opaque presentation arguments. Their vector order does not determine their placement, and literal braces
inside a value are never interpreted again. Both roles must be present exactly once and nonempty; bounded arguments and
the shared checked fixed-domain bridge reject missing/duplicate/unexpected roles, bad enums, unsafe controls, malformed
UTF-8 and oversized input before publishing any result. Tones and provenance distinguish the original named roles after
layout, clipping, wrapping and color. Unknown requested languages do not acquire an invented requested review status.
ASCII/disabled-backend recovery renders the whole English template with explicit recovery values, never translated
fragments mixed into an English sentence. Failure preserves the previous text/metadata/rows together.

Both arguments of refresh/record-count messages must be atomic-LTR human presentation spans. A refresh quantity includes
its explicit `ms` unit in its own span for both before and after; no conversion, unit inference or preference parsing is
performed. The legacy native byte output still uses its original asymmetric `500 -> 1000 ms` spelling, supplied explicitly
to the same source template, while the independent typed fixture uses complete `500 ms` / `1000 ms` quantities. This is not
activation of new native formatting. Boolean and severity values can use the separate [field-value catalog](ui-field-messages.md)
with explicit English recovery, but are never used to reconstruct stored bools, severity classes, timestamps or dirty state.
Even equal displayed values do not authorize the renderer to invent whether a preference changed.

Tests cover all 256 texts, stable keys, locale/review/direction parity, both named roles, forward/reverse English spelling,
opaque braces, exact expanded-output limits, numeric/unit atomicity, all three order and paragraph-direction policies,
five widths, painted/plain equivalence, wrapped row completeness and transactional refusal in all four typed wrappers.
A private generated comparison uses the fixed pre-extraction class, not updated golden output: **61440 complete native
frames** match byte-for-byte, and **393216 legal drafts** (13 booleans, four refresh values, four record limits, three severity
values) compared against default preferences in both directions give **786432 identical complete change vectors**. The
frame fixture now also flips the default-false timestamp toggle. This verifies strings/order, not real terminal behavior,
all pairs of arbitrary nondefault states or physical disk/worker qualification.

Exact platform/sanitizer/privacy evidence is in the [execution ledger](architecture/execution-plan.md). Existing shell,
field, settings/display/runtime/supervisor, CLI and PTY regressions remain separate. Root action notices, subpanels,
dashboard, diagnostics/raw domains, effective native typed binding, ICU data-owner/deployment, real terminal/font/shaping/
accessibility and human translation review remain unfinished. No production GPU/game/pool/financial/release gate is closed
by these fixtures, and no ordinary renderer loads ICU or activates a requested draft language.
