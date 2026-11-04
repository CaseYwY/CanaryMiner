# Typed provenance and color after Unicode layout (P-13f)

M-I18N/M-UI; R-I18N-002; T-I18N/T-UI/T-PRIVACY; D-104.

**Independent line adapter, not ordinary native language activation.** The existing bounded terminal core now optionally
returns source-span runs from the very same grapheme/cell/bidi pass. No substring search or reparsing of painted output is used
to rediscover a character’s style. The ordinary byte-based dashboard still does not call the new layout/segment API or link
ICU; full-domain/typed-number/native activation and data-owner/terminal/human gates remain open.

`layoutTerminalTextRuns` returns the unchanged `TerminalTextLine` plus typed runs carrying UTF-8 text, source-span index,
declared cells and an internal-control flag. Runs concatenate byte-exactly to the line text and sum exactly to its cells;
the adapter verifies both invariants before transactional publication. Original input bounds, whole-grapheme clipping,
atomic invariant ASCII, independent ambiguous/emoji widths, orphan replacement and logical-versus-visual modes all reuse
the previous core. No incoming ANSI or bidi controls are admitted. At most 4226 runs fit the 4096-byte/64-span/internal-isolate
budget; adjacent runs with the same source are coalesced. Any failure leaves the old result untouched.

Logical-mode internally generated direction isolates are distinct zero-cell control runs, not translated/styled source
characters. Visual reordering emits each complete grapheme with its original span index, including mirrored scalar output and
orphan replacements; invariant ASCII remains byte-exact and atomic. A style boundary inside a grapheme still refuses. This is
render-only provenance, not permission to parse the result back into machine identities or financial quantities.

`TerminalSegment` attaches the existing native `Tone` to a validated human or invariant span. `layoutTerminalSegments` maps
that tone through source provenance, even after visual reordering, while internal direction controls stay unstyled. Invalid
tone/color enum values refuse rather than manufacturing escape codes. `renderTerminalSegments` then emits only the existing
fixed palette’s SGR opens/resets, after layout, within a separate 65536-byte output cap. No caller escape, arbitrary color code,
environment lookup, global locale or terminal capability guess is accepted. `ColorLevel::None` emits byte-exact plain layout;
the caller still owns `NO_COLOR` and capability selection. Meaning is carried by text/provenance, never color alone.

The existing `Tone`, `ColorLevel`, palette and legacy `paint` implementation move to lightweight `terminal_style.h` without
importing dashboard/telemetry/network/files/platform APIs. Legacy behavior, including its explicit box-rule flag and status
versus telemetry palette choices, remains byte-equivalent. The new adapter uses literal painting: it **does not** turn `-` into
an ambiguous-width box glyph after cells have already been measured. Existing ordinary Rule painting is not silently changed.

Fixtures compare all 208 catalog texts across three order modes, three directions and six widths, typed provenance/cell totals,
atomic identity clipping and all four color levels. Stripping only internally generated SGR must recover the exact plain line;
RTL Hebrew/combining/orphan cases check original tone assignment, and a 4096-cell case checks coalescing and bounds. A separate
legacy palette oracle covers eight tones, four color levels, both box flags and empty/ASCII/UTF-8 text, while dashboard golden
regressions protect unchanged ordinary output. Final platform/sanitizer/corpus/privacy evidence is recorded in the
[execution ledger](architecture/execution-plan.md).

This supplies line provenance/color plumbing only. Typed localized-number isolation/atomicity, colored wrapping/padding,
message-argument roles, complete plural/date/time/all-domain extraction, actual native renderer binding, explicit ICU data
ownership/deployment, real terminal/font/shaping/accessibility and human review remain unqualified. Existing ASCII invariant
rules must not be bypassed by falsely labelling a localized number or arbitrary UTF-8 identifier as invariant ASCII.
