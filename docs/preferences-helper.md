# Private isolated preference-service foundation (P-14p)

M-CONFIG/M-PRIVACY; R-UI-007, R-SAFE-001/002, R-PRIV-001/002; T-CONFIG/T-PRIVACY/T-INTENT; D-173.

This slice supplies a helper-side service and strict private protocol, **not yet ordinary UI/control-loop integration**. The existing live settings file path remains synchronous until the separate background owner and native completion/review integration are implemented and verified. Merely linking/building this component starts no helper, collection, mining or network operation.

## Frozen targets and private frames

An inherited `DuplexTaskChannel` binds the helper to its actual parent. No CLI file path, shell command, credential discovery, device/game query or provider is accepted. `CMPREFS1` Init freezes exactly eight explicitly configured target slots: UI, runtime, manual references, electricity, game, pools, language and tariff. Empty slots remain unavailable. Nonempty paths must be bounded, absolute and lexically normalized; duplicate lexical paths refuse. This is the same user-privilege local application, **not** an OS filesystem sandbox or proof against malicious aliases/parent-directory writers.

Init constructs stores but reads/creates nothing. Each explicit Load opens only its selected slot. A successful Load returns a canonical private document and ticket-bound review. Save requires the matching current review and a canonical typed valid original; the empty tariff template can be loaded for editing but is not a valid Save. Target changes, duplicate/replayed/out-of-order tickets, unknown keys/versions/kinds, malformed controls/UTF-8, missing fields and oversized input refuse. Existing schemas, migration/backup, confirmation timestamps, tariff/version checks and P-14o failed-reopen rules remain authoritative.

The one-line wire is bounded to **192 KiB**, with **65536-byte** documents and eight **4096-byte** targets. Escaped canonical maximum documents fit without changing shared public/IPC limits. Native framing accepts one batch frame; the parent must separately enforce one-flight admission. Fixed status enums contain no raw errors/paths/private values. Private documents are neither diagnostic/export material nor public stats/logs.

## Save uncertainty and original preservation

The helper proves its complete Saved response is representable **before mutation**. A successful store result returns the exact requested canonical bytes under the new ticket. A failed Boolean store Save is conservatively **Unknown**, because its old error interface cannot prove whether a replacement occurred. No error-string guessing, deletion, repair or automatic retry is added. The old review is invalidated even if another actor restores identical bytes; explicit Load/review is required again. Rejected structural/stale input never reaches Save and does not consume a healthy loaded store's review.

Cancellation/parent death can race an already issued filesystem operation. Transport/reaping and retained write uncertainty therefore remain separate from a current setting or physical durability. Native process identity/quarantine comes from the existing duplex implementation; this helper does not create a new global reset/retry permission.

## Executed evidence and limits

Linux ordinary/own-code ASan+UBSan passes **117 protocol / 249 service / 45 actual inherited-helper** checks. Actual helper tests create all eight stores in owned scratch, traverse a **>64-KiB (about 131-KiB) escaped request** through partial native writes, preserve originals across conflicts and require fresh review. Non-inherited worker invocation refuses without stdout/stderr. Windows default-off full CTest passes **430/430** with the three new cases; repetition/privacy completion is recorded in the architecture ledger after execution.

The initial standalone protocol test omitted its own `<vector>` include; that compile failure was corrected before either profile ran. No real configuration, pool/account, installed miner, GPU/game, physical storage/stall, hard native bound, production packaging or ordinary settings latency qualification is inferred. The background owner/native settings bridge remains the next software task.
