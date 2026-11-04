# Fixed root settings diagnostic envelopes (P-13p)

M-I18N/M-UI; R-I18N-001/002; T-I18N/T-UI/T-CONFIG/T-PRIVACY; D-114.

**Fixed source extraction plus separate typed draft rendering, not raw-error translation or scrubbing.** Seventeen
`settings.diagnostic.*` keys across sixteen packs (272 texts) cover seven no-target/load-error pairs, the interactive-terminal
requirement, unavailable development energy helper and settings-closed notice. Every non-English entry remains a
machine-assisted draft. Ordinary root controls and CLI output select English source only; exit codes, stderr/stdout,
newlines, terminal-control sequences and the load/save/stop/helper lifecycle are unchanged.

There are deliberately two different boundaries:

- Native literal envelopes use English-only source helpers. For a load error the source helper extracts **only a fixed
  English prefix** from a catalog template ending in `{error}`. It never receives or processes the error. The existing
  native expression still appends the original raw store error byte-for-byte; no truncation, reinterpretation, translation,
  new validation or invented fallback is inserted into that legacy route. The original and its existing rendering/logging
  restrictions remain independent. Other raw save/load/backend errors are not claimed to be extracted or safe.
- The independent typed wrappers require the complete template with exactly its expected named arguments. An `error`
  presentation must independently satisfy the shared bounded UTF-8/control/role/recovery policies. A safe recovery cannot
  hide an unsafe original. Missing/duplicate/extra roles, bad enums, oversized input or row limits reject the entire result
  without changing previous text/metadata/rows. Values containing `%s` or `{error}` stay opaque; no formatting/parsing runs
  inside them. An explicitly supplied empty error is permitted, matching the prefix-only legacy case. Non-ASCII errors
  without explicit ASCII recovery refuse ASCII/disabled rendering; no transliteration, stripping or invented diagnosis.

The typed API does not read stores or paths, invoke OS error translation, grant data access, redact identifiers or classify
raw errors. Source extraction is not a privacy guarantee: callers still own admission and private-original handling. No
ordinary settings path calls this typed API or activates a requested language/ICU backend. The literal `settings` command
token remains stable in every terminal-requirement text.

Tests cover all 272 texts, stable keys/review/direction/placeholders, exact English prefixes, opaque synthetic bytes,
order/direction/width/color/row matrices and failure preservation in all wrappers. Native store tests exercise all seven
control-opening functions with null targets and malformed **owned synthetic files**; expected English envelopes remain
separate from the actual parser error, failed opens preserve original bytes/preferences and open no subpanel. Actual
host-stub CLI and PTY regressions protect redirected rejection, closing text, save/cancel and stop behavior. Existing
fixed-predecessor frame/review/transition comparison remains applicable to the root renderer, not a claim that its
predecessor-class harness also exercises external control-opening functions. Exact evidence is in the
[execution ledger](architecture/execution-plan.md).

Remaining subpanels, dashboard/log/raw-error domains and effective typed native/data-owner/deployment integration are
unfinished. Human review, physical terminal/font/shaping/accessibility and real GPU/game/pool/power/financial/release
qualification remain open. No production miner or existing user configuration is changed by these fixtures.
