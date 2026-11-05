# Runtime field and change envelopes (P-13s)

M-I18N/M-UI; R-I18N-001/002; T-I18N/T-UI/T-CONFIG/T-PRIVACY; D-117.

**English-only native source extraction and separate typed drafts, not runtime activation.** Thirteen stable
`runtime.field.*`, `runtime.change.*` and `runtime.value.*` keys across sixteen packs (208 texts) cover compact/configured
wait/history fields, current-source variants, next-start-with-same-flags projection, both review-change rows, activation
envelope and CLI/default source labels. Existing on/off labels reuse the earlier [field-value catalog](ui-field-messages.md).
Runtime shell/editor prompts, limitation/error messages, activation-value descriptions and raw input remain separate work.

Native Runtime settings calls English-only source helpers, with exact previous bytes, row/field ordering and current/
configured/next-session distinctions. No wait is applied, collection toggled, session restarted or file saved by a renderer.
The same existing `effectiveRuntimePreferences` computes the hypothetical same-flags projection; a configured draft never
replaces the immutable current worker/session. A zero-valued CLI wait remains an explicit override, not an absent optional.
Disabling collection still preserves existing history and does not infer actual collection availability or delete records.

Templates have named `wait`, `history`, `source`, `before`, `after` or `activation` roles. Bounded English helpers check exact
roles and printable-ASCII arguments, expand only the original source once and cap the entire result. Independent typed
`wait` arguments and both sides of a wait-change row require **atomic-LTR human quantities** with their own explicit `ms`
unit, so clipping/wrapping cannot separate a visible number from its unit. No conversion, duty-cycle inference or settings
parsing is performed. Native review deliberately keeps its old asymmetric `0 -> 1000 ms` spelling by explicitly supplying
the original values, while the typed fixture supplies whole `0 ms` / `1000 ms` quantities. This is not native localized
numeric formatting or application of a saved work-wait policy.

Human history/source/activation arguments retain their own explicit English recovery and roles/tones, never parse back
into booleans, CLI flags or activation enums. Keyboard identifiers `Enter`/`Space` stay stable while their bracketed
explanations may be translated; literal `CLI --no-history` remains intact. All non-English texts remain machine-assisted
drafts. Whole-English ASCII/disabled recovery, unknown-language review metadata and transactional text/row/color refusal
reuse the existing checked fixed-domain bridge. Neither API reads preferences, mutates workers nor starts an ICU backend
for ordinary rendering.

Fixtures check all 208 texts, key/locale/review/placeholder/protected-token parity, exact English, bounded nonrecursive
expansion, zero/maximum numeric spans, before/after atomicity, role/tone provenance, order/direction/width/paint matrices,
wrapped rows and invalid transactions. A private fixed-predecessor harness checks **122880 complete Runtime frames** and
**23040 key transitions/resulting frames/default serializations**, including idle/running contexts, both history flags,
CLI combinations with an explicit zero wait, compact/full layouts and the contextual editor. Save access stays empty and
only six representative valid waits are used; this is not exhaustive wait-space, real save-concurrency, worker/hardware or
physical terminal qualification. Actual native store/CLI/PTY/supervisor regressions remain independent. Exact platform/
sanitizer/privacy evidence is in the [execution ledger](architecture/execution-plan.md).

Remaining Runtime shell/editor/notices, other subpanels/dashboard/raw domains and effective typed native/data-owner/
deployment integration are unfinished. Human review, physical terminal/font/shaping/accessibility and real GPU/game/pool/
power/financial/release gates remain open; no production miner replacement or restart is authorized by these fixtures.
