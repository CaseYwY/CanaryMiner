# Message-key and locale-pack foundation (P-13a)

M-I18N; R-I18N-001/002; T-I18N/T-PRIVACY; D-99.

This is the first bounded message-domain slice, **not the completed internationalization milestone or native terminal activation**. All sixteen declared locale IDs have compiled common navigation/state/language/review messages: `en`, `zh-Hans`, `zh-Hant`, `es`, `pt-BR`, `fr`, `de`, `ru`, `ja`, `ko`, `ar`, `hi`, `bn`, `id`, `tr`, `vi`. Coverage is currently **13 common keys per pack**, not the hundreds of settings, financial/safety explanations, errors and logs still to be extracted. Ordinary UI/CLI/protocol output is not translated by this batch.

English is recorded as **source**, not falsely certified as externally human reviewed. Every non-English pack is explicitly **machine-assisted draft**; no human review or native-speaker approval is invented. Arabic is recorded as an RTL language; that flag is metadata, not a claim of bidi/shaping or terminal support. Locale IDs use exact stable matches. Unknown IDs, unsupported regional variants and missing/invalid translations fall back to English with explicit returned fallback/source/review metadata; locale selection never discovers geography or selects a utility/currency/timezone.

`MessageKey` maps only to stable ASCII human-message IDs. Flags, JSON properties, event codes, pool/protocol bytes, assets and financial calculations remain outside the catalog. Formatting locale, tariff jurisdiction/plan, currency, reporting clock and asset/machine identity are not language-derived. No process-global `setlocale`, environment detection, file/network lookup or owner activation is performed.

## Templates and bounded output

Templates use named ASCII `{source}` and `{value}` parameters rather than printf or position-based substitution. Translation validation preserves the exact placeholder multiset; malformed braces, unknown names or dropped/duplicated originals refuse. Formatting accepts the exact argument set once, never reparses argument contents as a format string, preserves invariant ASCII URLs/numbers/asset labels byte-for-byte, and caps final output at 4096 bytes. Unknown/duplicate/missing arguments, malformed UTF-8, selected control/line-break/bidi-override injection and capacity failures leave output/metadata unchanged. Literal `%` and argument braces are not executable formatting syntax.

The strict UTF-8 scalar validator rejects invalid leads/continuations, overlongs, surrogate scalars, out-of-range scalars, C0/C1 controls and explicit line-break/bidi-override/isolate controls. It **does not** segment graphemes, measure terminal cells, shape Indic/Arabic text, perform Unicode normalization or implement UAX #9. A scalar-valid string must not be byte-clipped or rendered into an unqualified terminal merely because it passes validation. Likewise a successful template is not an authorization to reorder/translate its invariant identity spans.

## Remaining Unicode and native work

Before native selection is enabled, replace the renderer's byte clipping/wrapping with a maintained grapheme/cell contract, explicit ambiguous-width/emoji policy, no-color/ASCII fallback and bidi-aware layout with invariant identity spans. Distinguish logical-order terminals from terminals needing visual ordering; do not double-reorder. Plural rules/complete templates, independent numeric/date formatting, native settings/persistence/CLI precedence and all remaining message domains need implementation and tests. Real terminal/font/RTL/assistive-technology and human translation review remain separate qualification, not facts supplied by this CPU catalog.

The dependency review has checked ICU's [character-boundary contract](https://unicode-org.github.io/icu/userguide/boundaryanalysis/) (UAX #29 character boundaries are grapheme clusters) and [bidirectional guidance](https://unicode-org.github.io/icu/userguide/transforms/bidi.html). Grapheme count is not cell width; mirroring/reordering is not a complete glyph renderer. These references are requirements input, not an adopted/built/packaged dependency or passing native rendering evidence.

[P-13b](terminal-text.md) subsequently adopts a separately disabled ICU line core with explicit width/bidi/ASCII policies. It still does not activate this catalog in the native byte-based UI or qualify physical fonts, shaping or terminal accessibility.

[P-13i](terminal-message.md) subsequently adds typed named common-template arguments and whole-template English recovery,
including separate requested/rendered review/capability status. It does not add complete domains/plural sentences or activate
ordinary native translation.

[P-13j](terminal-count.md) subsequently adds a separate two-key/six-category count domain in all sixteen packs (192 template
slots), still machine-assisted for non-English text. The thirteen common keys and their historic counts are unchanged.

[P-13l](settings-messages.md) subsequently extracts a separate thirty-key settings-shell/group domain (480 texts). Native
settings intentionally binds only its English source; independently typed draft rendering is not effective translation.

[P-13m](ui-field-messages.md) subsequently adds twenty-five root Display/Details/Log field/notice/value keys (400 texts),
again with English-only native source binding and separate typed draft rendering, not all-domain or effective activation.

[P-13n](settings-change-messages.md) adds sixteen complete root-review change keys (256 texts), with named before/after
roles, unchanged native English change vectors and separately typed numeric/unit presentation. Remaining domains and
effective native/data-owner/terminal/human gates remain open.

[P-13o](settings-action-messages.md) adds thirty-four root action/notice keys (544 texts), retaining explicit privacy,
capability/refusal and effective-language limitations. English native routes/bytes are unchanged; this does not cover
control-opening/TTY diagnostics, subpanels or effective typed native activation.

[P-13p](settings-diagnostic-messages.md) adds seventeen fixed root diagnostic envelope keys (272 texts). English-only
prefix extraction does not touch native raw originals; separate typed values need safe-original and explicit recovery
admission. Raw-error translation/privacy, subpanels and effective native activation remain unqualified.

[P-13q](language-field-messages.md) adds eleven Language field/review-description keys (176 texts), with unchanged native
English frame bytes and independent machine request values. A translated review description never certifies the pack;
Language shell/other domains and effective native/data-owner/terminal/human gates remain open.

## Executed test scope

`test_localization` validates all 16 IDs/autonyms/review/RTL metadata and 208 current key translations, exact source/translation placeholder parity, explicit English fallback, output/metadata preservation, no printf/reparse, both exact 4096-byte template boundaries and alias-safe output. An exhaustive generated scalar corpus covers all values through U+10FFFF, plus malformed/truncated byte sequences and control negatives. That exhaustive scalar check still does not prove grapheme/cell/bidi/formatting behavior. Exact platform counts, sanitizer and privacy results are in the [execution ledger](architecture/execution-plan.md).
