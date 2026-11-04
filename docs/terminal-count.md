# Complete maintained count templates (P-13j)

M-I18N/M-UI; R-I18N-001/002; T-I18N/T-UI/T-PRIVACY; D-108.

**Independent two-key count domain, not full UI coverage or ordinary activation.** `canary_terminal_count` adds complete
Workers and AcceptedShares templates to the typed message/number/row boundary. Its sixteen packs have two keys and six
category slots each (192 slots, with intentionally repeated forms); the existing thirteen common keys per pack are unchanged.
Every non-English count pack remains a **machine-assisted draft**, not human reviewed. The ordinary native renderer still
uses legacy English/per-field formatting and does not link or call this adapter. No GPU, worker, network or mining operation
is performed by a count render.

## Exact count and independent language/format policies

`TerminalCountRequest` requires an explicit canonical nonnegative ASCII integer, at most 96 digits: `0` is valid, while missing,
signed, fractional, exponent, leading-zero, whitespace, non-ASCII and oversized counts refuse. Missing is **not zero**. The
existing exact core and `fractionDigits=0` retain that integer without double conversion or display rounding. The result owns
the complete `PresentationNumber`, including fixed ASCII, actual formatting/plural language, fallback/resource metadata and
plural availability, alongside the typed or painted message result. Returned count text is human presentation, not authority
to create counters, records, accounting credit, observed power or a financial fact. An accepted-share count does not establish
attributable income, even if a translated label renders successfully.

Message language, formatting locale, digit style, grouping, terminal order/direction and widths remain separate inputs. A
French sentence can contain Arabic-Egyptian digits, for example, without changing its cardinal grammar. Normal Unicode
selection uses the existing maintained ICU cardinal selector on the **formatted exact integer**; there is no numeric rule
implementation in the catalog or adapter. The catalog's uniform/binary authoring shorthands only fill text slots and never
inspect a count. English source templates use an independently maintained English category, not the selected language's
category (French `0` and Arabic `2` must not dictate English grammar). Native formatter errors still refuse rather than
silently switching to a guessed category.

The [ICU C API](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/upluralrules_8h.html) defines the six cardinal
keywords and `uplrules_selectFormatted`; formatted visible fraction digits/exponents can affect selection. The existing number
core passes a `UFormattedNumber` produced from the fixed decimal with rounding unnecessary. Count requests cannot introduce
fractional visible digits. Broader decimal cardinal primitives remain available separately, not coerced into integer counts.

## Explicit neutral recovery, not guessed English plurals

When the Unicode backend is disabled, or terminal order explicitly requests ASCII, complete neutral English templates are
selected: `Workers: {count}` and `Accepted shares: {count}`. The result sets `neutral=true`, keeps `pluralAvailable=false` and
an **empty** category, and reports the actual rendered English source status separately from the requested draft language.
It never substitutes `other` or an own-code English singular rule. Explicit ASCII number recovery sets `asciiRecovery=true`,
skips native ICU formatting/selection calls and does not mislabel this explicit choice as a backend failure. Separately linked
ICU library loading is not made hermetic or qualified by this own-code no-call path.

Unsupported but syntactically safe requested languages use explicit English fallback with no invented requested review status;
numeric language fallback is retained as well. Unsupported formatting IDs separately record formatting fallback. Malformed
tags, invalid enums/policies and invalid counts refuse transactionally. Recovery does not infer a currency, asset, tariff
jurisdiction, timezone, device/worker identity or protocol/donation choice.

## Typed composition, publication and limits

The count is one atomic-LTR **human** argument named `count`, not an invariant ASCII identifier. Its complete localized value
survives RTL ordering, clipping and autonomous wrapping; a too-wide wrapped number refuses the whole row group. Literal and
count tones are independent, original argument provenance remains inspectable and internal direction controls stay unstyled.
Painting still happens only after layout. The adapter returns number, message metadata and neutral status in one owned result;
failure preserves the previous entire result. All original raw/painted byte, span, grapheme, column and row limits remain.

The common message implementation now exposes an internal **fixed-domain** template-pair bridge, not a runtime user-template
or review-authority API. Before rendering either mode it validates English/selected placeholder parity, including occurrence
counts, and malformed/unsafe templates refuse even if ASCII recovery would otherwise choose English. The public common API
is unchanged. This bridge neither certifies translation quality nor makes caller metadata a security credential.

## Validation scope and remaining work

Fixtures check every catalog slot's exact `count` placeholder/review metadata, all 31 formats ×16 languages ×two digit styles
×two keys ×eight counts, explicit Arabic six-category/Russian one-few-many branches, >2^53 exact integers, grouping, all
terminal orders/directions, atomic clipping, original tones, complete neutral recovery, owned-result failure and unchanged
process locale. Empty/negative/fractional/leading-zero/invalid/97-digit inputs, unsupported tags, invalid enum/policy, row/group
and null-output refusals are explicit. Common bridge malformed/parity/unsafe negatives and prior common-message matrices are
regressions. Exact final Windows/Linux ordinary/sanitizer/artifact/privacy evidence is in the
[execution ledger](architecture/execution-plan.md). First strict GCC compilation required an explicit `<optional>` include and
separating misleadingly indented statements; that failed compilation is not passing evidence.

This completes these two count templates only. Date/time and other UI domains, actual native binding, explicit ICU data-owner/
deployment, human translation review and real terminal/font/shaping/accessibility remain unfinished. External ICU is not
sanitizer-instrumented by own-code tests. No real GPU/game/pool/financial, physical storage/terminal or production qualification
is inferred from synthetic CPU counters and strings.
