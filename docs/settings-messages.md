# Settings-shell domain extraction (P-13l)

M-I18N/M-UI; R-I18N-001/002; T-I18N/T-UI/T-CONFIG/T-PRIVACY; D-110.

**Source extraction plus independently rendered drafts, not native translation activation.** The native settings root now
obtains its existing English shell text from stable `settings.shell.*` / `settings.group.*` keys. Eleven navigation groups
and nineteen title/review/save/leave/focus/resize/footer messages form a **30-key shell domain**, with all sixteen packs
(480 texts). Every non-English pack remains machine-assisted draft, not human reviewed. Field labels, change descriptions,
settings subpanels, dashboard, diagnostics and raw worker text are **not covered by this shell domain**.

The ordinary byte-based settings renderer explicitly calls English-only source helpers; it does not select a requested
language, use the independent Unicode adapter or claim that catalog presence means effective translation. All existing group
indices, state-machine keys, save targets, current-parent requests, stop/discard semantics and ordinary output are unchanged.
Language requests still visibly report legacy English/per-field formatting, and saving a language still does not load ICU.

## Catalog and typed boundary

`settingsMessage(language,key)` validates complete-template placeholder parity and returns requested pack or explicit English
fallback with source/draft/RTL metadata. Keys are stable ASCII identities, never translated. Exact bracketed keyboard tokens
are retained in all packs; translating human instructions never changes the physical key bindings. The title retains the
CanaryMiner name. The root UI's Saved text describes its already-live **presentation controls**, not the Language subpanel's
separate request-only save; these contexts are not interchangeable.

`ReviewHeading` and `GroupHeading` use named `count` and `group` arguments rather than translatable string fragments. The
historic English review-heading wording remains byte-identical, including its existing `changes` word for a single change;
this source-preservation extraction does not invent or claim a newly maintained review-count grammar. The separate
[Workers/AcceptedShares count domain](terminal-count.md) remains independent.

`settingsEnglish` / `settingsEnglishField` serve only the legacy source binding. The latter supports just these two known
single-parameter English templates, requires printable ASCII input and enforces the 4096-byte expanded bound. Inserted braces
remain opaque and are not reparsed. It is not a locale formatter or a route for non-English text through byte clipping.

`canary_terminal_settings_message` separately supplies typed layout/wrap/paint wrappers using the existing internal fixed-domain
template bridge. Argument names, cardinality and original safe text/roles/tones are checked; language/name/value/count bounds
are checked before copying the request. Complete English templates are selected before explicit ASCII/backend recovery, not
assembled from translated fragments. Unknown requested languages have no invented requested review status. Text and metadata
publish together, controls stay unstyled and typed atomic values/whole-row refusal retain prior contracts. No runtime user
template, global locale, file, network, currency/timezone/asset inference or worker/mining action is introduced.

## Validation scope and remaining work

Fixtures check all 480 texts, exact IDs/keys/reviews/placeholder and bracketed-key parity; all three orders/directions and five
widths; actual versus requested language/status; source/argument tone provenance; complete English recovery; painted/plain
equivalence and transactional row/invalid/bound/null-output refusal. The protected English source templates preserve original
punctuation, spacing, notices and save-status meanings. Existing native settings/display/language/CLI/supervisor regressions
remain applicable because source extraction touches the ordinary root.

A private generated CPU comparison uses the fixed predecessor's actual `SettingsPanel` class, renamed only to coexist with
the current class, and synthetic inputs. It compares **61440 complete native frames byte-for-byte** across every root group,
field, Browse/Review/Leave state, focus, dirty state, message, four widths/colors and five heights. This is stronger than
replacing old expected text with a newly generated snapshot, but still not physical terminal or human review evidence.
The private harness and generated source/binaries are not shipped or required by public test discovery. Exact final
Windows/Linux/sanitizer/CLI/PTY/privacy evidence belongs to the [execution ledger](architecture/execution-plan.md).

This is the shell domain only. Remaining field/subpanel/dashboard/log/diagnostic extraction, full native typed binding,
explicit ICU data-owner/deployment, real terminal/font/shaping/accessibility and human review remain unfinished. Native
English-source extraction supplies no GPU/game/pool/financial/source/release qualification or automatic restart permission.

[P-13m](ui-field-messages.md) subsequently extracts the separate Display/Details/Log field/notice/value domain. The shell's
thirty keys remain unchanged; remaining action rows, change descriptions and subpanels are not silently counted as covered.

[P-13n](settings-change-messages.md) subsequently extracts the sixteen complete root-review change templates. Its own
source/draft and predecessor evidence does not change the historical thirty-key shell scope or cover root action notices.
