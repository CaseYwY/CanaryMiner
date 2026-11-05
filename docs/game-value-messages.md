# Game field/review/input envelopes (P-13x)

M-I18N/M-UI; R-I18N-001/002; T-I18N/T-UI/T-CONFIG/T-PRIVACY; D-123.

**English-only native source extraction and independent typed drafts.** Seven stable `game.envelope.*` keys in sixteen
packs (112 texts) cover field/value, before/after, separate human-scope value/change, edit prompt/input and effective frozen
mode. All non-English entries remain machine-assisted drafts, not effective native translations or human-reviewed UI.
The earlier [field labels](game-field-messages.md) and [literal shell](game-panel-messages.md) remain separate domains.

The native Game panel uses bounded English expansion, preserving old punctuation, field/page/scroll order, contextual
Q/D/L/S input, byte-tail clipping, save/cancel/review, frozen current session and machine preference serialization. Expansion
accepts at most three exactly named arguments, each printable ASCII and at most 128 bytes; duplicate/missing/extra names,
controls and invalid keys refuse. Arguments expand once, so braces in private input are literal, never recursive templates.
Raw parser/store/callback errors are not translated or interpreted. Neither expansion nor a saved draft starts scanning,
a game, a worker or a session.

`TerminalGameValueMessageRequest` and four independent layout/wrap/paint wrappers separate machine requests from human
descriptions. FieldValue/FieldChange values, EditInput input and EffectiveFrozen mode **must** be invariant ASCII; scope
value/change arguments **must not** be invariant, because Windows-session/Linux-native-UID descriptions are human labels,
not serialized adapter identities. Human arguments retain their own explicit ASCII recovery. Each argument's original and
recovery bytes are capped at 128 before shared validation, including disabled-backend recovery. Invalid UTF-8, control
injection, substituted invariant recovery, duplicate/missing/extra names, invalid policy/tone/row limits and null outputs
refuse transactionally. Atomic invariant values never partially clip; independent rows retain provenance and tone.

Timing values here are saved **invariant integer requests**, not localized quantities or observed times. Literal `ms` in a
field label does not claim localized numeric/unit binding. The display's 128-byte limit is not numeric-input admission:
the unchanged native editor buffers at most eight timing characters, accepts at most five integer digits and applies the
existing field-specific range. Target editing retains its separate 128-character ASCII-basename rules. No display output
is parsed back into preferences or becomes detection, identity, safety, Coexist or mining authority.

Fixtures cover all 112 texts, exact English/key/parameter/review/RTL parity, one-pass/reordered/empty/128-byte expansion,
human/invariant role refusal, every order/direction/width, plain/paint/source/cell provenance, independent wrapping and
unchanged outputs on failure. The fixed predecessor Game harness remains **124416 complete frames / 44064 transitions,
resulting frames and active/draft serializations**, with empty save access; native store/CLI/PTY/supervisor checks are
separate. Exact platform/sanitizer/privacy results are in the [execution ledger](architecture/execution-plan.md).

This does not bind effective native typed rendering or live data owners, deploy ICU, complete other subpanels/dashboard/
logs/raw domains, review translations or qualify physical terminals/fonts/shaping/accessibility. Real GPU/game/pool,
financial/power/safety sources, global helper quarantine and production release gates remain open.
