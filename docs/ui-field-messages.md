# Display, Details and Log field messages (P-13m)

M-I18N/M-UI; R-I18N-001/002; T-I18N/T-UI/T-CONFIG/T-PRIVACY; D-111.

**English source extraction plus independent draft rendering, not effective native translation.** This adds a separate
25-key domain in sixteen packs (400 texts): fifteen root Display/Details/Log field templates, five explanatory notices and
five on/off/severity value labels. Stable keys are `settings.field.*` / `settings.value.*`. All non-English entries remain
machine-assisted drafts. The actual native settings renderer calls English-only source helpers and keeps its previous
spacing, group/field indices, preferences, review/save/stop behavior and full frame bytes. Merely adding catalogs never
changes the effective language reported by the Language request panel.

`{state}` and `{value}` are opaque named arguments, not translated fragments or preference parsers. The typed field wrappers
reuse the settings/common fixed-domain bridge and publish text plus requested/rendered review/fallback metadata together.
They retain the existing argument/byte/span/row caps, pre-copy bounds, safe-original validation, source tones and whole-English
ASCII/backend recovery. Unknown requested languages have no invented requested review status; invalid requests preserve the
previous whole result. English source helpers are printable-ASCII-only and enforce the expanded output cap.

Checkbox states in the typed boundary must be exact invariant ASCII `x` or a single space. Timestamp-format selectors must
be invariant `ISO UTC` or `UTC HH:MM:SS`; this describes the legacy Log display option, not selection of a system zone or
activation of the separate timestamp adapter. Numeric refresh/record fields require atomic-LTR presentation. The refresh
argument includes its **explicit `ms` label in the same atomic span**, so clipping/wrapping cannot leave a number separated
from its unit. Number strings still carry no preference/source authority; the renderer does not parse them back into settings.
`NO_COLOR` remains the literal environment-control token in every explanatory translation, not a translated variable name.

This does not change boolean/severity storage or log classification. Raw worker lines stay unclassified unless the existing
source admission classifies them; a translated label is never used to infer severity. Disabling a view/follow option still
does not stop draining or collecting data, and main safety/status visibility remains governed by the existing model. Nothing
in these messages starts/stops workers, loads ICU for the ordinary renderer, changes sampling/mining intensity, controls a
driver or infers a tariff, currency, asset or timezone.

Fixtures check all 400 texts and key/review/placeholder parity, protected `NO_COLOR`, both checkbox marks, exact English
spacing, numeric/unit atomicity, named argument tones and logical/visual/ASCII policies across widths/directions. They check
whole-row and malformed/unsafe/type/capability/unknown-language refusal, source-only helpers, opaque values and painted/plain
equivalence. The private predecessor-class comparison additionally flips all relevant Details/Log/display options and checks
**61440 complete native frames byte-for-byte**, including legacy clock-format choices. Current shell/common/native settings,
display, CLI, supervisor and PTY regressions remain applicable. Exact platform/sanitizer/privacy counts are recorded in the
[execution ledger](architecture/execution-plan.md); synthetic frames are not real terminal/font/shaping/accessibility tests.

This covers these three root field groups only. Remaining action rows, review-change descriptions, subpanels, dashboard,
diagnostics and raw text still need their own domains and source-to-typed binding. Explicit ICU data-owner/deployment,
human review and physical terminal qualifications remain unfinished, and no GPU/game/pool/financial/release qualification is
inferred. The legacy byte renderer remains English until complete integration is qualified.
