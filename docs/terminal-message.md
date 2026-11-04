# Typed named common messages and complete-template recovery (P-13i)

M-I18N/M-UI; R-I18N-001/002; T-I18N/T-UI/T-PRIVACY; D-107.

**Independent common-domain message adapter, not ordinary UI activation or full translation coverage.**
`canary_terminal_message` connects the existing thirteen-key/sixteen-pack catalog to typed source spans and the bounded
line/row/color core. It is not linked or called by ordinary dashboard/settings/worker/protocol paths. No money, currency,
tariff, timezone, source fact, worker action or donation behavior is inferred. A successful standalone render reports its own
actual rendered language; it does not change P-13e’s still-legacy native effective presentation.

`TerminalMessageRequest` supplies a stable catalog key, independent exact requested language tag, literal tone and at most
eight **uniquely named typed arguments**. The existing checked template contract requires the exact argument set. Literal
catalog fragments and argument spans stay separate; braces/percent signs inside an argument are never reparsed as a template
or printf expression. Argument human/invariant/atomic-number roles, explicit recovery bytes and tones survive substitution,
clipping, visual reordering and wrapping. Metadata maps source-span indices back to original argument names; empty names mean
catalog literals, not inferred machine keys. Direction controls and generated padding remain distinct non-source roles.

## Whole English recovery, not fragment-by-fragment pseudo-English

On explicit ASCII order or a disabled Unicode backend, the adapter selects the **entire English source template before
composition** and then selects each argument’s explicit ASCII recovery. This preserves English punctuation/argument order even
when the selected non-English template differs; it does not splice guessed English fragments into translated grammar. Invalid
original controls/UTF-8/types/invariants are validated before choosing a safe-looking argument fallback. Recovery cannot hide
an unsafe original or invent a missing argument representation.

Normal Unicode rendering can split a trusted catalog sentence into fragments that have no meaningful independent English
fallback. `TerminalTextRecovery::UnicodeOnly` explicitly expresses that fact: the span has no fallback and refuses ASCII order,
disabled Unicode, invariant-ASCII misuse, a competing fallback or an invalid recovery enum. All old scalar/control/grapheme/
size/handling rules still apply. It never silently substitutes `?` or fabricates transliteration. The message adapter keeps
these fragments internal and chooses the complete English template itself when recovery is needed. Existing callers retain
the default `RequireAscii` contract. A caller-supplied Unicode-only argument cannot be recovered without an explicit valid
representation and therefore refuses rather than disappearing.

`TerminalMessageMetadata` separates requested and actually rendered language/review, unsupported-language fallback, explicit
ASCII recovery, backend fallback and catalog RTL metadata. An unknown requested language has **no requested review status**;
it is not falsely stamped English-source or human-reviewed. Non-English packs remain machine-assisted drafts. Catalog RTL
does not select terminal order/direction, widths, digits, currency or geography. English source status is not independent human
certification. Safe unsupported tags use explicit English catalog fallback; malformed/injected tags and invalid keys refuse.

## Publication and bounds

`layoutTerminalMessage` / `wrapTerminalMessage` return typed lines/rows with their metadata. Painted equivalents return raw
painted text/rows and metadata **in one owned result**, avoiding conflicting output/metadata aliases and partial updates.
Failure preserves the caller’s previous whole result. The underlying 4096-byte expanded input, 64-span, 4096-cell, whole-grapheme/
atomic clipping, row/output/SGR bounds remain enforced; a too-large message is not silently truncated into a valid request.
Wrappers reuse autonomous row semantics rather than claiming rich-paragraph reflow. Color remains post-layout and optional.

The adapter accepts only existing catalog keys, not runtime user templates or ICU expressions, and makes no own-code global-locale,
environment, file, network or mining call. In explicit ASCII recovery it does not request Unicode backend/data initialization;
separately linked ICU loading/initialization may still involve external runtime data and is not qualified by that own-code claim.
Unicode backend/resource/deployment and real terminal/font/shaping/accessibility qualifications remain separate gates.

## Validation scope and remaining work

Fixtures cross all 208 common texts, ordering/direction/width policies, requested/rendered draft/source status, literal/argument
tone provenance, exact fixed numeric/unit arguments and complete English fallback. They check French-versus-English punctuation,
unsupported format-like language IDs, Arabic visual codepoint order (not shaping), unchanged braces/percent arguments, exact
4096-byte expansion, missing/extra/duplicate names, invalid original controls/types/tags/keys/tones, transactional row failure
and Unicode-only capability/refusal. Exact final platform/sanitizer/corpus/artifact/privacy evidence belongs to the
[execution ledger](architecture/execution-plan.md).

Full translated plural sentences, date/time domains, all UI-domain extraction, actual native binding, explicit ICU data
ownership/deployment, human review and physical terminal/font/shaping/accessibility remain unfinished. No ordinary native
language, real financial/source/GPU/game/pool or production qualification is supplied by these synthetic CPU fixtures.

[P-13j](terminal-count.md) subsequently reuses an internal checked fixed-domain template-pair bridge for complete Workers/
AcceptedShares forms, with independently maintained English selection and neutral recovery when plural rules are unavailable.
The public common-message API remains unchanged; this is not runtime user-template or full-domain/native activation.
