# Maintained grapheme and bounded terminal-line presentation (P-13b)

M-I18N; R-I18N-002; T-I18N/T-UI/T-PRIVACY/T-RELEASE; D-100.

**Independent default-off development core, not native UI activation.** `canary_terminal_text` provides bounded line preparation separately from the current byte-based dashboard. The ordinary miner/settings/supervisor do not link or call it. Enabling `CANARY_ENABLE_TERMINAL_ICU` requires separately supplied ICU 79.1+ `i18n`, `uc` and `data`; configure performs no download or installation. With the option OFF, no ICU headers, native symbols or libraries are required and a requested Unicode line uses explicit ASCII fallback with `BackendDisabled` metadata. The version floor alone is not security or deployment qualification.

## Text, bounds and layout

The input is at most 64 owned spans and 4096 UTF-8 bytes, with a separately bounded explicit printable-ASCII fallback. A non-ASCII human span must supply that fallback rather than pretending automatic transliteration is a reviewed translation. Invariant spans are printable ASCII and cannot supply a different fallback. Input controls, malformed scalars, line breaks, incoming bidi marks/overrides/isolates, noncanonical policy enums and unsupported width values refuse transactionally. This renderer boundary is stricter than the catalog's general scalar validation. Backend errors also refuse without replacing the previous output; they are not silently relabeled as successful Unicode rendering.

The explicit policy has 0..4096 columns, ambiguous characters at one or two cells, and recognized RGI emoji sequences at one or two cells. ICU root `UBRK_CHARACTER` supplies extended grapheme boundaries over the complete line, not one independent byte/string slice at a time. A span/tone/identity boundary inside a cluster refuses. Wide/fullwidth properties use two cells; other visible bases contribute the maximum declared base width in the cluster. Combining/default-ignorable units do not independently add cells. A cluster consisting only of marks/invisible units becomes a visible one-cell `?` with `replacedOrphan` metadata instead of modifying the previous line/cell. ICU's maintained UTS #51 string membership identifies RGI emoji; a homemade ZWJ/keycap heuristic is not used.

Clipping keeps a prefix of complete graphemes. Invariant spans (amount/asset/time labels, URLs, machine keys) are atomic: if the entire span cannot fit it is omitted, never shortened into a different amount/asset/endpoint. The result reports `cells`, `clipped`, replacement and fallback status and is at most 8192 bytes including internally added directional controls. All failure paths preserve the previous result. The lower-level `terminalGraphemeBoundaries` returns UTF-8 byte offsets and deliberately accepts scalar-valid conformance controls; that is **not** permission to send those controls to a terminal. Its disabled backend supports only printable ASCII and is not advertised as Unicode conformance.

Column measurement is an explicit terminal policy, **not a universal Unicode truth or physical font measurement**. The width rule is a deterministic base/emoji convention, not a glyph shaper: Indic/Arabic ligatures, non-RGI emoji sequences, terminal-specific ambiguous/emoji behavior and fallback fonts can differ. A text's correct algorithmic segmentation alone does not qualify its terminal width or accessibility. Locale selection still cannot infer these policy settings from geography or language.

## Logical versus visual ordering

Logical mode is for a terminal that performs bidi itself. Invariant spans receive controlled LRI/PDI isolation; an explicit paragraph direction adds the corresponding outer LRI/RLI/PDI. Auto direction leaves the paragraph decision to that terminal. The output is render-only and must not go through byte clipping, protocol serializers or machine-key consumers.

Visual mode is only for a terminal that does **not** perform bidi. ICU `ubidi_setPara` calculates levels and a visual map with internal identity isolation. The implementation maps complete graphemes into their contiguous visual ranges, refuses any directionally discontinuous cluster and emits each cluster intact in visual order. Odd-level scalar mirroring uses ICU; invariant ASCII remains left-to-right and byte-exact. Internally inserted isolate controls are omitted from visual output. Do not pass this visual result through auto-bidi a second time. Neither mode performs glyph shaping or claims Arabic/Indic/font correctness. There is no automatic terminal capability probe or global `setlocale`/`uloc_setDefault` change.

## Development dependency provenance

Official release: <https://github.com/unicode-org/icu/releases/tag/release-79.1>. Downloaded artifacts were SHA-256 matched against that release's GitHub asset digests, not independently authenticated publisher signatures. These are correlated HTTPS/release-service checks, not two independent provenance witnesses. Full upstream license and third-party notices are retained in [ICU-79.1.txt](../LICENSES/ICU-79.1.txt); no library, implementation or test dataset is vendored.

- `icu4c-79.1-sources.tgz`: `06fa63ecb521048f8082f12ad988f6b678d90cbd88aafce24cbc94902b191d99`.
- `icu4c-79.1-Win64-MSVC2026.zip`: `c4ae1cbadb4187007f76f8b1bcf3a10deb699395ab3c74df01362bfdfa0f1ac2`.
- Source `LICENSE` (LF bytes): `e09827667e0bb3c808b2a463548ba814a5e6249b67353234d70c8dfc2bcc417d`. The Windows archive's CRLF notice has digest `2eb5ef4da31e542030455e2d149feca8d8f1217e69ef98ea7e379067043b79ab`; normalizing only CRLF produces the same LF notice. Its raw mismatch was investigated, not waived.
- Source `source/test/testdata/GraphemeBreakTest.txt`, labeled Unicode **18.0.0**: `b0cf047ee94485bbdc846de2b902f5f8a815f6b674f9d04223cddadd91c9df31`.

Windows development uses the official x64 DLL/import-library bundle, explicitly supplied outside the repository; it does not use or replace the system ICU. Linux development builds that source in a private prefix using `runConfigureICU Linux --enable-static --disable-shared --disable-tests --disable-samples --disable-extras --with-data-packaging=static` and links the static data/core/i18n libraries. The external ICU implementation itself is not sanitizer-instrumented. Platform DLL/data search, plugin/environment policy, lazy initialization/IO, servicing, exact redistribution inventory, independent signature authentication and clean-machine qualification remain release gates before ordinary activation. No claim of bounded OS/disk initialization is made from 4096-byte own-code limits.

## Validation and remaining integration

Native fixtures cover transactional invalid/control/size/policy rejection, zero/full column boundaries, explicit ASCII fallback/reason, wide/combining/Indic/emoji samples, orphan replacement, cross-span cluster refusal, atomic invariant clipping, Hebrew combining marks, mixed RTL/numbers/URLs/emoji and all 208 common catalog messages across three order modes, three paragraph policies and six widths. An explicitly supplied ICU corpus exercises the UTF-8/UTF-16 offset wrapper against **853** upstream cases; it is never fetched automatically. Corpus/algorithm passes are not human review or real terminal evidence. Exact platform counts and repetitions belong to the [execution ledger](architecture/execution-plan.md).

P-13c subsequently adds bounded autonomous-row wrapping and padding below. Colored segment/native renderer integration, independent formatting/plural messages, native language/persistence/CLI precedence, all UI domains, terminal capability/fallback discovery, startup data-owner policy, Windows/Linux terminal/font/assistive-technology qualification and human translations remain open. Existing byte helpers must not be used for non-English activation in the meantime. Reference contracts: [ICU boundary analysis](https://unicode-org.github.io/icu/userguide/boundaryanalysis/), [ICU bidi](https://unicode-org.github.io/icu/userguide/transforms/bidi.html), [ICU4C build guide](https://unicode-org.github.io/icu/userguide/icu4c/build.html).

[P-13d](localized-format.md) subsequently supplies exact independent number/cardinal primitives, not complete plural translations, date/time formatting or native activation.

[P-13f](terminal-segments.md) subsequently adds source-provenance/cell/control runs and native fixed-palette color after line layout, without ordinary activation or typed localized-number/colored-row qualification.

[P-13g](terminal-number.md) supplies atomic exact numeric/unit human spans and [P-13h](terminal-rows.md) carries original
provenance into colored autonomous rows/physical fields. These do not activate the ordinary native renderer or qualify full
messages, rich-paragraph reflow, data ownership or real terminals.

## Autonomous rows and padded fields (P-13c)

`wrapTerminalText` delegates enabled Unicode line opportunities to ICU's root `UBRK_LINE`, intersected with the maintained grapheme edges and atomic invariant spans. Disabled/requested ASCII uses explicit fallback text and ASCII-space opportunities. It prefers the last complete opportunity that fits the declared cell width; long human words can break only at grapheme edges. It preserves all original whitespace, unlike the old ASCII helper. Each output is an **independent display row/bidi paragraph**, suitable for separate native fields, not a rich-text paragraph with preserved cross-line embedding/bracket context. It does not claim full paragraph reflow/UAX #9 `setLine` equivalence.

No invariant is split across rows. A single too-wide grapheme or invariant refuses the whole operation, rather than losing it, inserting a false shortened identity or looping on an impossible width. Zero columns, invalid row bounds, exceeded 1..4096 row budget or total 32768-byte output also refuse without publishing partial rows. Empty input produces one empty row. All rows reuse the line core and explicit direction/width/fallback/orphan metadata. No wrap operation starts networking, performs currency/time arithmetic or changes the process locale.

`padTerminalText` first applies the same safe clipping, then adds printable spaces to exactly the requested cell width, left- or right-aligned. Logical mode wraps an FSI-isolated content block in a controlled LTR physical field so that padding is not confused with the text's language direction. Visual/ASCII output adds no bidi controls. The independent 8192-byte final-output bound still applies, including combining sequences and internal controls; padding failure preserves the original result. This is a render-only field, never a wire identifier or machine-format value.

Fixtures cover maintained Unicode opportunities/whole graphemes, long-word fallback, lossless spaces, all-or-nothing row/too-wide/zero-width rejection, atomic multirow identities, three direction policies, combining/wide/emoji/Arabic/Indic/Bengali rows, left/right padding, exact 4096-row bounds and combining-output capacity. These are algorithmic row/field checks, not colored segment integration, rich-text paragraph reflow, physical terminal/shape/font/accessibility or native-language activation.
