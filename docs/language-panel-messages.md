# Language shell, review envelopes and inactive-state notices (P-13r)

M-I18N/M-UI; R-I18N-001/002; T-I18N/T-UI/T-CONFIG/T-PRIVACY; D-116.

**English-only native source extraction plus independent typed drafts.** Thirty-two stable `language.panel.*` keys in
sixteen packs (512 texts) cover the Language title/footers/short-window/leave/save/draft text, complete field and before/
after/current review envelopes, independent-policy/worker/CLI notices, explicit inactive/effective fallback descriptions
and legacy-number-format label. The eleven earlier [field/review labels](language-field-messages.md) remain a separate
historical scope. Grouping's existing on/off source values reuse the [field-value catalog](ui-field-messages.md), not new
duplicate keys. Parser/store/raw validation/save errors and machine policy names remain independent.

Native code calls English-only helpers and preserves its old complete frames, row ordering, selection/scrolling, returned
status text, raw-error path and saved/frozen/effective request semantics. `effectiveNativeNumberFormat()` and
`nativeLanguageFallbackReason()` retain their original `const char *` API and exact English bytes, backed by static whole
source literals rather than temporary strings. Neither method discovers an OS locale, changes settings or activates a
Unicode backend. New tests keep the source pointers valid across other pack lookups and verify unchanged machine JSON.

Whole templates have named `field`, `value`, `review` or `format` roles. English source helpers validate exact bounded role
sets and printable-ASCII values, expand only the original template once and cap complete output; braces inside arguments
stay opaque. The independent typed boundary requires every `value` role (field/before/after/current machine request) to be
**invariant ASCII**, not an atomic localized number or translated policy token. Human field/review/format arguments retain
their own explicit English recovery and source tones. Rendered strings never become preference, parser or effective-state
authority. ASCII/disabled recovery selects a complete English template; unsafe originals cannot hide behind recovery.
Unknown-language metadata, clipping, wrapping, paint and invalid/row refusal remain transactional under the existing bridge.

Keyboard tokens and the stable `[End language]` marker are preserved in every pack, together with
`language-preferences.json`, `CLI`, `ASCII`, source common-key count and short-window threshold. The legacy notice saying
13 **common** keys refers only to the original common-key domain, not the total across later catalogs or full UI coverage.
All non-English entries remain machine-assisted drafts and no ordinary source calls a typed renderer. Requested translation
review descriptions never certify themselves; current presentation still explicitly says English/legacy and requests not
active. Format locale still cannot change assets, currency, timezone, electricity, donation, protocols or mining behavior.

Fixtures check all 512 texts, exact keys/reviews/roles/protected tokens, source limits and nonrecursive expansion, machine
invariance and no fallback substitution, role/tone provenance, order/direction/width/paint matrices, complete wrapped rows
and refusal in every typed wrapper. The fixed pre-extraction private Language harness still checks **184320 complete
frames** and **34560 transitions/resulting frames/serialized active-draft requests** with empty save access. Ordinary native
request/store/CLI/PTY/supervisor regressions remain separate; this harness does not qualify every locale/policy combination,
actual concurrent save writers or physical terminal behavior. Exact platform/sanitizer/privacy evidence is in the
[execution ledger](architecture/execution-plan.md).

Other subpanels, dashboard/log/raw domains, effective typed native/data-owner/deployment binding, human review and physical
terminal/font/shaping/accessibility remain unfinished. These fixture results do not qualify live GPU/game/pool/power/
financial/release behavior or authorize production replacement/restart.
