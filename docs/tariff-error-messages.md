# Whole offline-tariff error envelopes (P-13bc)

M-I18N/M-UI; R-I18N-001/002; T-I18N/T-UI/T-CONFIG/T-PRIVACY; D-154.

Five `tariff.error.*` keys in sixteen packs (80 templates) separate path/root errors with opaque original reasons,
path/root errors with caller-supplied human prose, and already formatted raw errors. Explicit root forms preserve the
legacy leading `: ` without inventing an empty invariant path. Every non-English pack remains a machine-assisted draft,
including punctuation-only templates; no human review or effective native translation is claimed.

Native offline-tariff validation selects source English OriginalPath/OriginalRoot after the existing UI-only classified
reason bridge. Nonempty failed-save messages select Raw; callback text is never parsed at a colon, classified, translated
or substituted again. Existing empty-error fallback, retained messages, error focus, draft/review/active state, save
predicates and stop behavior remain unchanged. Model/parser/store code imports no UI catalogs or typed layout.

The source-only English helper deliberately preserves original bytes, including empty, oversized and unsafe developer
states. This preserves compatibility, not safe output, schema admission, sanitization or disclosure permission. Existing
native byte wrapping remains unchanged and does not acquire Unicode/terminal qualification from this extraction.

The independent `TerminalTariffErrorMessageRequest` and four layout/wrap/paint wrappers have a stricter display contract:

- Actual nonempty paths are invariant ASCII, at most 512 original/recovery bytes.
- Original reasons are nonempty invariant ASCII, at most 1024 original/recovery bytes.
- Human reasons are nonempty non-invariant prose, at most 1024 original/recovery bytes, with explicit ASCII recovery when
  required; ordinary human wrapping is allowed. A caller choosing this role does not prove the description's truth.
- Already formatted Raw reasons are nonempty invariant ASCII, at most 4096 original/recovery bytes. Unsafe/non-ASCII raw
  originals refuse rather than being hidden by a safe replacement or parsed into human fragments.
- Shared aggregate input/output bounds, exact unique argument names, UTF-8/control/tone/policy validation, complete English
  recovery and transactional publication still apply. Invariant originals stay whole, are omitted on clipping or cause
  wrapping refusal; none may become a truncated different path/reason.

Fixtures cover all templates, review/RTL/parameter parity, one-pass raw preservation, actual root/path validation,
memory-only failed callbacks, independent human/original roles, all direction/order/paint modes, retained source/cell
provenance, wrapped rows, maximum bounds and unchanged outputs on refusals. No callback opens storage. Fixed-predecessor
native comparisons and full native/model/store/CLI/PTY/platform/sanitizer/privacy evidence are recorded in the
[execution ledger](architecture/execution-plan.md).

These envelopes neither authorize private disclosure nor supply supplier/model/coverage/source/action facts. Other
legacy/raw/panel/dashboard/log domains, complete effective native/data-owner/deployment integration, human review and
physical terminal/font/shaping/accessibility remain open, as do actual safety/game/GPU/financial/TLS sources, physical
durability, global quarantine and production qualification.
