# Human activation descriptions and ASCII helper boundary (P-13u)

M-I18N/M-UI; R-I18N-001/002; T-I18N/T-UI/T-CONFIG/T-PRIVACY; D-119.

**English-only native source extraction plus independent typed drafts, not activation.** Five stable
`activation.description.*` keys in sixteen packs (80 texts) describe live presentation, a safe work boundary, next explicit
start with no automatic restart, separate permission and unsupported input. All non-English entries remain machine-assisted
drafts. These human labels neither prove a boundary safe nor grant permission, enqueue work, apply settings or restart a
session. Existing stop/safety/CLI/collection and current/configured/next-session rules retain sole authority.

`activationName(ChangeActivation)` retains its `const char *` API, all exact original English strings and unknown enum to
`unsupported` recovery. Explicit switch mapping keeps the action enum separate from catalog keys; no translated text is
parsed back into either. Static whole source literals keep returned pointers valid across later catalog lookups. Native
Runtime review still renders English; the new argument-free `TerminalActivationMessageRequest` and its four layout/wrap/
paint wrappers are independent, not ordinary renderer or data-owner binding. Complete English ASCII/disabled recovery,
unknown-language review metadata, source-tone provenance and transactional refusal reuse the existing typed bridge.

## Low-level source boundary

`activation_source.h` is ASCII-only, uses standard headers only and owns the five keys and exact English source literals.
The English catalog pack reuses those same literals. `runtime_preferences.h` includes only this source boundary, not the
multi-language catalog or localization header. Low-level native helpers therefore do not acquire UTF-8 catalog parsing,
locale discovery, Unicode backend linkage or a rendering dependency merely to obtain an English description.

An initial implementation incorrectly included the full catalog through `runtime_preferences.h`. On Windows' default
CP936 source code page, a native helper without `/utf-8` then failed compilation. A minimal ASCII translation unit including
only that preferences header reproduced the failure; the same unit with `/utf-8` compiled, and strict UTF-8 decoding ruled
out damaged catalogs. The permanent `activation_ascii_consumer` target was observed failing before the source split and
compiling/running afterward, as did the originally failing helper. The consumer deliberately has **no UTF-8 compile switch**.
The fix isolates the new dependency rather than broadening compiler flags or suppressing diagnostics. Artifact guards also
check ASCII source bytes, absence of project-header includes and the preferences-to-source/catalog separation. This is a
regression for the new include edge, not a certification of all existing transitive headers or every system code page.

Fixtures cover all 80 texts and key/review/placeholder parity, exact English/invalid-source lookup, order/direction/width/
paint/wrap matrices, unknown language and all-wrapper invalid-output preservation. Native Runtime tests add twelve exact
description, unknown-enum, retained-pointer and unchanged-serialization assertions. The existing fixed-predecessor Runtime
frame/transition comparison and actual host-stub CLI/PTY checks remain separate from real workers or physical terminals.
Exact platform, sanitizer, fresh-build and privacy evidence is in the [execution ledger](architecture/execution-plan.md).

Other subpanels/dashboard/log/raw domains, effective typed native/data-owner/deployment binding, human review and physical
terminal/font/shaping/accessibility remain unfinished. Host/CPU fixtures do not qualify live GPU/game/pool/power/financial/
release behavior, solve global helper quarantine or authorize production replacement/restart.
