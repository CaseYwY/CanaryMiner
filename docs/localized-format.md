# Independent exact number presentation and cardinal rules (P-13d)

M-I18N/M-TARIFF; R-I18N-001/002, R-TAR-001; T-I18N/T-TARIFF/T-PRIVACY; D-102.

**Independent development presentation library, not ordinary native activation.** `canary_localized_format` is not linked by the ordinary miner or called by settings/dashboard/CLI serializers. The same explicit default-off `CANARY_ENABLE_TERMINAL_ICU` switch selects separately supplied maintained ICU; no dependency is downloaded, no system ICU is replaced and no global locale is selected. The dependency/notices/provenance and unqualified data-loading/deployment boundaries in [terminal text](terminal-text.md) still apply.

## Exact inputs, one display rounding boundary

`PresentationNumberRequest` carries an explicit strict ASCII decimal, 0..24 visible fraction digits, grouping, Latin-versus-locale-default digit style, **formatting locale**, and a separate **message language**. Existing bounded `ExactDecimal` parsing and `formatRatio(1, places)` perform explicit half-even **display** rounding once. The original input is not mutated. The result retains the exact fixed ASCII rendering and a `rounded` comparison flag; it never passes through binary floating point, guesses asset atomic precision, converts currencies or changes source quantities.

The maintained ICU path receives that already rounded fixed ASCII decimal through `unumf_formatDecimal`. Internally constructed skeletons require the exact visible fraction count and `rounding-mode-unnecessary`; user text is not a skeleton, format expression or locale extension. ICU provides number symbols/grouping/digit presentation and cardinal plural operands, not a second arithmetic policy. Bounds are the existing 96 coefficient digits/48 input scale, 24 visible places, fixed native output buffers and transactional output publication. Invalid numeric syntax, nonfinite/exponent/grouped/user-localized input, invalid policy enum or tag injection refuses without publishing a partial result.

This output is human presentation, not a replacement for API/JSON/protocol numeric serialization. `PRL`, `USDT`, `USD`, tariff jurisdictions, utilities, timezones, asset identity and any conversion rate are absent from the request. Append their separately typed invariant labels through the renderer rather than treating the decimal as an authorization to translate an asset or invent a price. Localized number direction/isolation still needs native colored-span integration; numeric strings alone do not qualify RTL terminal rendering.

## Independent locale, language and digit decisions

The explicit formatting list contains the sixteen common language IDs plus `en-US`, `en-GB`, `fr-FR`, `de-DE`, `es-ES`, `ar-EG`, `hi-IN`, `bn-BD`, `zh-CN`, `zh-TW`, `ja-JP`, `ko-KR`, `id-ID`, `tr-TR`, `vi-VN` (**31** entries). The public list is available for a future independent selector. A formatting ID does not select a country/utility/account/currency/timezone; it only chooses a reviewed formatting-data lookup. Message language matches the sixteen catalog IDs independently. Safe but unsupported IDs explicitly fall back to `en` with separate `localeFallback`/`languageFallback` metadata; malformed or injected tags refuse.

Changing message language cannot change number text or the fixed ASCII value. Changing the formatting locale cannot change the message language, source value or arithmetic. Latin digits are the explicit default. **Locale digits** means ICU/CLDR's declared default, not a script inferred from language: in the tested ICU 79.1 data `ar` uses `latn`, while explicitly selected `ar-EG` uses `arab`; `bn`/`bn-BD` use `beng`. A minimal `unumsys_open` probe confirmed that distinction before correcting the overbroad draft assertion. No user region was inferred.

## Maintained cardinal selection and resource status

ICU `uplrules_selectFormatted` selects the separately requested **language's** cardinal category from the same formatted number, including its visible fraction digits. Thus English displayed `1` can select `one`, while visible `1.0` selects `other`; Japanese and Arabic retain their own maintained categories. Only `zero`, `one`, `two`, `few`, `many`, `other` may be returned. This is a category primitive, **not completed translated plural sentences or grammatical review**. Full message branches/templates, ordinal/date/time presentation and native preferences remain separate work.

When ICU is disabled, `text` is the exact fixed ASCII value and `backendFallback` is explicit. Grouping/localized digits are unavailable, `pluralAvailable` is false and the category is empty. A guessed handwritten multilingual rule or fabricated `other` must not be substituted. A future caller must display that capability/fallback state rather than pretending it has a maintained plural result.

ICU's `U_USING_DEFAULT_WARNING`/`U_USING_FALLBACK_WARNING` on internal number resources is recorded separately as `resourceInheritance`, not confused with an unsupported requested locale. A minimal real ICU probe shows `en`, `de-DE` and `ar-EG` all reporting an inherited-resource warning while still producing their different requested decimal/grouping conventions. A draft blanket warning rejection first broke the matrix; it was replaced by explicit resource metadata, not silent suppression. Native hard errors still refuse. Exact resource-loading/provenance/clean-machine qualification remains open; metadata is not permission to accept an unknown or damaged release dataset.

ICU-created bidi pattern marks are removed from numeric presentation only after native formatting; caller-supplied marks never enter through the strict decimal/tag inputs. All resulting text is validated before publication. The subsequent render layer owns directional isolation. Neither `setlocale` nor `uloc_setDefault` is called; tests assert that the process locale remains unchanged.

## Executed scope and remaining work

Fixtures check half-even positive/negative/tie/zero cases, the `2^53+1` decimal boundary without binary loss, 96-digit/24-place limits, transactional numeric/tag/policy refusal, separate fallback/resource metadata, German/French/Indian/Egyptian formatting, locale-default digit differences, language-independent numeric output, visible-fraction cardinal categories and every declared formatting locale crossed with all sixteen languages, both digit policies and bounded sample values. Exact platform/sanitizer/artifact/privacy evidence belongs to the [execution ledger](architecture/execution-plan.md).

No real financial fact, profit, tariff, currency conversion, native language preference, full plural message, public machine-format change, terminal/font/shaping/assistive-technology or production qualification is established. Native selection/persistence/CLI precedence, all-domain extraction, dates/times, complete plural branches, colored/typed span integration, explicit data-owner policy and terminal/human review remain open.
