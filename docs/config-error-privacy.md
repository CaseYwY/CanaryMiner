# Closed startup-config rejection output (P-14x)

M-CONFIG/M-PRIVACY; R-UI-007/008, R-PRIV-001/002; T-CONFIG/T-PRIVACY; D-181.

The actual [background startup inspection](startup-config.md) no longer forwards the strict config parser's error string to the terminal. Unknown keys, malformed pool declarations and other invalid values can contain private originals or terminal controls. All private file parse failures now use one fixed ASCII envelope:

```text
config file rejected: invalid or unsupported config-v1 (schema_version 1 required); private details omitted; original preserved.
```

This intentionally trades automatic field/value detail for no accidental original/control-byte disclosure. The public `config/config-v1.schema.json` remains the format reference. No English diagnostic parsing, regex masking, partial/truncated original or automatic private preview is used. Missing/unreadable files, oversized files and unavailable helper admission retain their distinct fixed refusals.

Only the display boundary changes. The strict parser and its internal error-output API, validation order, exit code 3, source file, failed caller output, valid configuration/defaults/CLI precedence, donation and protocol rules remain unchanged. No repair, write, retry, network/device setup or source capability is added. A valid-file positive control must still reach the inert CPU device sentinel rather than being rejected by a blanket privacy guard.

The actual-entry regression uses only owned synthetic files and the genuine sibling. It covers unknown root/nested keys (including raw invalid-UTF-8 key bytes), control bytes, private invalid pool/compression/network/semantics inputs, version/type/JSON/NUL failures, original preservation and separate read/size/valid controls. A raw invalid-byte worker value retains the existing later fixed semantic refusal: this change neither adds UTF-8 validation to the legacy structural reader nor merges its phases. Native checks retain both the safe startup envelope and the unchanged internal parser API. Exact red/green, platform, reused-versus-fresh and source-identity evidence belongs in the [execution ledger](architecture/execution-plan.md).

This is not universal CLI/log/runtime/config anonymization. Other legacy diagnostics and explicit private views, collection/disclosure/retention/vault controls, all-domain effective localization, actual sources, console/native/physical/deployment and production-release qualification remain separate and incomplete.
