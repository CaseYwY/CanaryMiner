# Background startup configuration inspection (P-14w)

M-CONFIG/M-UI/M-SUPERVISOR/M-PRIVACY; R-UI-003/007, R-SAFE-001/002, R-PRIV-001/002; T-CONFIG/T-INTENT/T-PRIVACY; D-180.

The actual main entry no longer calls the synchronous product-config file loader. Signals precede inspection; the idle command rejects a redirected terminal before reading its configured file. A temporary foreground wait remains responsive to Q/Ctrl+C/Esc while an owned background thread resolves the configured path, launches the fixed actual-module sibling `canary_preferences_worker`, receives the original bytes, parses the existing strict config schema and settles the helper. An empty config path uses pure existing defaults without locating a helper. Version and closed diagnostic export still bypass config inspection.

## Private read-only one-shot protocol

The same inherited duplex channel accepts a distinct **first-frame-only** `CMCFGQ1` request, not a ninth preference slot or new argv/path-search mode. The canonical request contains one positive ticket and one absolute configured UTF-8 target. After ordinary preference Init, config mode cannot replace that service. This is an internal helper-wire addition; public CLI/API/Stratum bytes and the existing eight preference kinds are unchanged.

The helper opens only that configured file in binary read-only mode. It neither searches, creates, rewrites, migrates, repairs nor supplies defaults after a read failure. Absolute anchoring preserves `..` resolution through symlinks rather than lexically collapsing or canonicalizing it. Existing protected local-path admission applies; Windows network/device/alternate-stream path forms are not newly enabled.

A bounded canonical `CMCFGR1` header declares Loaded, Unavailable or Oversized and an exact byte count, followed by raw original bytes, not an expanded JSON string. The maximum document remains the strict parser's existing **1 MiB**; a compile-time assertion binds both limits. Empty loaded files are distinct from missing files and still fail config parsing. The client rejects wrong tickets, malformed/overlong headers, missing/extra/truncated bytes and nonzero exit. Exact bytes plus EOF **and verified zero exit** are required, not a reply alone. Originals remain private to the owned transport; new transport/read/size failures disclose no raw path or payload. Existing schema/CLI validation diagnostics are not comprehensive config anonymization.

## Stop, budget and original admission

One ten-second software budget starts before thread/key setup and is checked before helper admission and after parsing. Queue/setup/read time never resets it. Stop revokes the original request, settles only its own helper and cannot continue into preference/device setup. Missing helper or unavailable inspection refuses with the existing configuration exit code, never a synchronous fallback. Caller output changes only on complete successful admission; unchanged defaults, CLI overrides, donation settings and pure validation retain their existing semantics.

The original native epoch is carried through the background thread and back into the entire main caller scope, with further checks before later device admission. Clearing a global uncertainty flag cannot renew an earlier observation. Helper process ownership/reaping still uses the existing retained/quarantine implementation. This is cooperative software responsiveness, **not** a hard OS spawn/wait/termios/console/kernel-stall or physical-storage deadline.

## Evidence and boundaries

The old actual inert entry fails an owned held-FIFO test before it can announce a cancellable inspection. The new test connects a writer only after the genuine helper has opened that FIFO, then checks stop or releases exact synthetic bytes. Native tests separately cover canonical envelopes, Unicode local files, empty/missing/exact-size/oversized originals, caller preservation, cleared old epochs and real inherited-channel malformed/truncated/wrong-ticket/nonzero replies. Neither harness starts a miner/device backend, uses real configuration, contacts a source or writes configuration through the product.

Exact tight, full-source and platform results are recorded separately in the [execution ledger](architecture/execution-plan.md). Broader config-review/legacy diagnostic privacy, console/arbitrary callback isolation, complete collection/disclosure/retention/vault controls, source/translation/native/GPU/game/deployment/physical and production-release qualification remain open. This slice does not complete the roadmap.
