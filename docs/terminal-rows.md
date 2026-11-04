# Typed autonomous rows and padded fields (P-13h)

M-I18N/M-UI; R-I18N-002; T-I18N/T-UI/T-PRIVACY; D-106.

**Independent typed row/field plumbing, not ordinary native language activation.** The bounded plain wrap/pad core now
optionally carries original source-span provenance into each autonomous row and physical field. Existing plain APIs reuse the
same wrapping implementation; styles are never rediscovered through substring matching or by parsing painted escape codes.
Ordinary dashboard/settings/worker/financial/protocol code still does not call these APIs or activate ICU/translations.

`wrapTerminalTextRuns` preserves the prior whole-grapheme/atomic-span line opportunities, whitespace, row counts and fallback/
orphan metadata. Each row’s local layout indices are mapped back to the **original input spans**, including spans that first
appear on a later row and empty inputs. Runs concatenate exactly to the row’s raw text and their declared cells sum exactly to
the row’s cells, checked before publication. The existing 1..4096 rows and 32768 total plain output bytes remain whole-group
transactional limits. Too-wide atomic human quantities or invariant ASCII still refuse all rows, not a shortened or partial
quantity. Each row remains an independent bidi paragraph; this is **not rich-text cross-line embedding/bracket reflow**.

`padTerminalTextRuns` applies the same clipping and physical LTR/FSI isolation as the existing plain padded field. Generated
padding is now a distinct `padding` run: printable spaces only, cells equal to byte count and **not a source span**. Internal
direction controls remain separate zero-cell `control` runs; control and padding flags cannot overlap. Source indices are
meaningful only on actual source runs. Final raw field output remains at most 8192 bytes; the 4226 line-run budget permits at
most five extra field-control/padding runs (4231). Failure preserves the previous complete field.

`wrapTerminalSegments` / `padTerminalSegments` attach the original validated native tone after provenance mapping, with an
explicit independent padding tone (Plain by default). Controls are never styled. `renderWrappedTerminalSegments` /
`renderPaddedTerminalSegments` add only the existing fixed-palette SGR after layout, with no glyph substitution. They accept
typed source spans rather than trusting caller-assembled painted rows. `ColorLevel::None` is byte-exact plain output; invalid tone/color/
alignment/policy, row/output limits or final paint failure preserve the caller’s prior whole result. Each painted row/field is
bounded to 65536 bytes and an entire painted row group to 262144 bytes. Caller `NO_COLOR` and terminal-capability choices stay
independent; neither a color nor a language implies capability, status or source authority.

The exact [numeric/unit segment](terminal-number.md) retains its atomic human-LTR role through row reconstruction. Unit labels
and machine ASCII remain explicit and unchanged, while localizable text can wrap at maintained opportunities/whole graphemes.
Padding cannot be mistaken for source amount/identity text in a typed consumer. Physical cell/font/shape behavior still needs
real terminal qualification; deterministic column conventions and a successful CPU renderer do not supply it.

## Validation scope

Fixtures cross all 208 common texts, three order modes, three directions and four widths. Plain/typed wrapping and both padded
alignments must agree byte-for-byte and in cells/clipping, with original tone provenance, separate padding/control runs and
plain recovery after removing only internally generated SGR. They cover group/too-wide/zero-row/max-row rejection, empty input,
the full 4096-row case, explicit invalid enums, mixed Arabic/Hebrew numeric/unit provenance, orphan replacement and final
combining-output capacity. An initial orphan assertion wrongly assumed the first run was a source character despite explicit
LTR control output; the final assertion checks source/control roles, tone, replacement metadata and cells separately.

Exact final platform/sanitizer/corpus/artifact/privacy evidence belongs to the [execution ledger](architecture/execution-plan.md).
These remain synthetic CPU algorithm/adapter tests, not full sentences/plurals/date/time/all-domain extraction, ordinary native
language binding, explicit ICU data-owner/deployment, human translations, physical terminal/font/shaping/accessibility or
GPU/game/pool/financial/release qualification.
