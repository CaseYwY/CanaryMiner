# Atomic exact localized quantities (P-13g)

M-I18N/M-UI; R-I18N-001/002; T-I18N/T-UI/T-TARIFF/T-PRIVACY; D-105.

**Independent display composition, not a financial fact or native activation.** `canary_terminal_number` joins the existing
exact number formatter to the typed line/color core. The ordinary miner/dashboard/settings do not link or call it. No provider,
wallet, currency conversion, asset precision, tariff, timezone, worker or donation behavior is added. ICU remains separately
supplied and default off; all [dependency/data-owner/terminal qualifications](terminal-text.md) remain open.

`formatTerminalQuantity` takes the complete independent `PresentationNumberRequest`, an explicit ASCII label and native tone.
The existing exact core performs one half-even display rounding; ICU, if enabled, supplies only symbols/grouping/digits and
maintained cardinal selection. The result retains the complete `PresentationNumber`, including original fixed ASCII rendering,
rounding, backend/unsupported/resource and plural-availability metadata, alongside the ready-to-layout segment. Its displayed
text and fallback are the formatted/fixed-ASCII number plus the **same explicit label**, never a translated or locale-inferred
currency/asset. The label is empty or at most 32 exact ASCII bytes from letters/digits and `._/%+-`; whitespace, control,
non-ASCII, invalid tone or invalid number requests refuse transactionally. A token is a display label, not source qualification
or an assertion that the named asset/unit exists. No amount, price or conversion rate is fabricated by this adapter.

## Atomic human text is not an invariant machine identifier

The line core now has a separate `TerminalTextAtom::LeftToRight` handling request for **human presentation**. It clips/wraps the
whole span or none and uses internal LTR isolation, while still retaining grapheme boundaries, tone provenance, explicit ASCII
fallback and all previous scalar/control/size/policy validation. This does not relax `invariant`: invariant spans still require
printable ASCII and cannot substitute another fallback. Marking a localized number as invariant ASCII still refuses. Arbitrary
human RTL text in an atomic LTR field is still subject to maintained bidi reordering inside that field; atomic does not mean
byte-invariant or machine-authoritative. The quantity adapter uses numeric presentation plus an explicit ASCII unit token.

The entire number and label share one atomic human span so narrow clipping cannot publish just half a number or silently
lose its unit. Too-wide wrapping refuses all rows, not a shortened quantity or partial row set. Existing plain wrap/pad carry
the handling request forward; physical padding uses the same declared cell count, independent of language. Logical output
retains controlled isolates; visual output emits the same formatted numeric/unit bytes in the tested mixed-RTL matrix without
double bidi. Post-layout color still follows original source runs. ASCII recovery selects the explicit fixed-ASCII quantity
rather than byte-clipping localized digits. Rendering is display-only; the retained ASCII numeric value stays separate from the
label and must not be recovered by parsing terminal text.

The adapter calls the formatter itself rather than trusting a caller-assembled localized string, but its public result is not
a security or provenance credential. UI/source admission must still keep unknown, stale and partial financial facts distinct.
No persisted arithmetic, machine JSON/API/protocol numeric field or global locale is changed.

## Validation and limits

Synthetic fixtures cross all 31 formatting IDs, sixteen message languages, Latin/locale-default digits, three ordering modes
and three directions. They check exact >2^53 half-even rounding, retained numeric metadata/explicit labels, whole-span clipping,
all-or-nothing wrapping, physical padding and provenance/tone inside Arabic/Hebrew surroundings. Invalid labels/types/policies
preserve prior output and non-ASCII invariant misuse remains rejected. Exact final platform/sanitizer/artifact/privacy and prior
line/segment/corpus regression evidence belongs to the [execution ledger](architecture/execution-plan.md).

These are deterministic host/CPU fixtures, not actual income, qualified rates, full plural/date/time messages, colored-row
reflow, ordinary native translation activation, real terminal/font/shaping/accessibility, physical GPU/game/pool or production
qualification. Full-domain extraction, complete sentence/argument roles, colored wrap/pad, explicit ICU data ownership/deployment
and human/terminal review remain separate work.
