# Native independent language requests (P-13e)

M-I18N/M-CONFIG; R-I18N-001/002; T-I18N/T-CONFIG/T-UI/T-PRIVACY; D-103.

**Native request controls, not full translation activation.** The original S settings panel and idle `canary-miner settings`
command now expose Language with keyboard-only navigation, explicit review/save/discard and stop. This editor uses ASCII
locale IDs and English status text only: it never byte-clips non-English autonyms through the legacy renderer. It loads no
ICU data and calls none of the independent Unicode/number primitives. Ordinary native presentation remains **English / legacy
per-field formatting**, even when a separately enabled development test target can use ICU.

## Independent requests and storage

`language-preferences.json` has exactly nine schema-v1 members: schema version, language, formatting locale, digits, text order,
direction, ambiguous cells, emoji cells and grouping. Language accepts the sixteen [common catalog](localization-catalog.md)
IDs; formatting accepts the separate 31-entry [number format](localized-format.md) list. Digits are Latin or locale-default,
order ASCII/logical/visual, direction auto/LTR/RTL, and ambiguous/emoji widths independently one or two cells. Defaults request
`en`, format `en`, Latin, ASCII, auto direction, one ambiguous cell, two emoji cells and grouping on. No global/OS locale or
language-driven region detection is used. The lightweight metadata header is shared with the number core without linking ICU.

The strict native parser rejects missing/extra/duplicate members, malformed JSON, unsupported IDs/enums, wrong types,
non-unsigned-integer numeric tokens and input above 4096 bytes. [The schema](../config/language-preferences-v1.schema.json)
documents the semantic shape; JSON Schema alone does not enforce native duplicate/token/byte restrictions. Parse failure
preserves the old output. This is an independent private local preference target: it neither rewrites main config/UI/runtime
preferences nor carries wallets, accounts, worker/device identifiers, tariffs, currencies, timezones, assets or protocol fields.

Saving reuses the existing private bounded regular-file store, per-file lease, original-byte compare-and-swap and exclusive
temporary replacement. Load must succeed before Save; an external writer, invalid file/lock or unsupported future schema is
preserved rather than repaired/overwritten. POSIX permissions/symlink refusal are tested. Windows uses native wide paths and
reparse-point refusal; inherited Windows directory ACLs still require deployment qualification. Synchronous filesystem/save
latency, kernel/disk failure and physical power-loss durability remain release gates, not proven bounded by a small document.

## Native review, CLI precedence and effective state

Arrows/Tab move, Left/Right/Space/Enter cycle one field, V/F2 opens a complete review, Enter/F2 saves, Esc edits/returns and a
dirty leave requires explicit Discard or review. Q/Ctrl+C stops without implicitly saving. Narrow review scrolls to a final
marker and survives resize; the focused field stays visible. The review includes every before/after value and, when applicable,
the frozen current-parent request, plus translation-review/fallback/worker invariants. A failed Save keeps the draft and old
active requests. Success says **Saved requests only**, never “translation activated.” The parent’s current request is frozen:
even saving while workers run changes neither it nor their lifecycle. Reopening reads saved requests independently.

`--language ID` / `--language=ID`, `--format-locale ID` / its equals form, and the valueless `--ascii-ui` override only their own
requested fields. Repeated options, unsupported exact IDs, empty values or a value attached to `--ascii-ui` refuse without
echoing rejected text. They do not silently persist or become another field’s default. ASCII is a request for future native
layout recovery, **not a claim that all current raw log/device bytes are sanitized ASCII**. Explicit requests on help/version
and other paths visibly report legacy effective presentation on stderr without loading preferences just for those commands;
JSON/machine output remains untranslated. Ordinary parent/idle settings also read saved requests; missing is default, and bad
or unreadable language files emit a generic preservation/English-fallback warning without blocking mining. Language settings
cannot bypass separate runtime/game/TLS/startup safety refusals, initiate mining, restart a worker or revive a stopped session.

Only thirteen common keys per pack exist so far. Non-English status stays **machine-assisted draft / not human reviewed**;
English source status is not independent certification. Saving a language is not full-domain coverage, maintained plural
sentences, date/time formatting, typed colored-span integration, terminal/font/bidi/shaping/accessibility qualification or
production deployment. Existing exact amounts, clocks, machine keys and upstream donation/protocol semantics remain unchanged.

## Validation scope

Own-code fixtures cross all sixteen languages/31 formats/two digit/three order choices, independent direction/width/grouping
policies, strict schema/parser failures, Unicode file paths, absent/corrupt/store/CAS/lock preservation, keyboard/root routing,
review/save/refusal/discard, focused short layouts and frozen current requests. Real host-stub CLI tests stop at an explicit
device sentinel (never real GPU setup), check bad-file fallback and no mutation/echo, and exercise help/ASCII/duplicate paths.
POSIX PTYs drive actual idle settings and the existing synthetic supervisor through save/discard/external conflict/resize,
SIGINT, frozen CLI requests and unchanged worker-start records. The initial CLI assertion conflated two correct fallback warning
forms; a minimal real-process probe distinguished them. The PTY now waits for a browse-only marker rather than a title shared
with the review page, preventing a stale frame from authorizing a second Esc.

Exact final platform counts, regressions and privacy evidence belong to the [execution ledger](architecture/execution-plan.md).
Linux uses strict direct GCC, not Linux CMake. These are host/synthetic checks, not production CUDA/GPU/game/pool/financial or
physical-terminal qualification. Native all-domain/typed-layout activation, complete plural/date/time messages, data-owner
policy, real terminal/font/accessibility tests and human translation review remain separate work.
