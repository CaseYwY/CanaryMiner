# Background initial preference inspection (P-14t)

M-CONFIG/M-SUPERVISOR/M-PRIVACY; R-UI-007, R-SAFE-001/002, R-PRIV-001/002; T-CONFIG/T-INTENT/T-PRIVACY; D-177.

The ordinary supervisor uses its original [background preference owner](preferences-owner.md) for initial UI, Manual and Electricity reads as well as [live settings](live-preferences.md). It retains no preference Store or synchronous fallback. Inherited-parent Init freezes all eight configured slots without opening them; the control loop then serially consumes only its own exact optional UI/Manual/Electricity Load tickets. Bootstrap never opens a panel, queues Save, writes missing defaults or reports Saved.

## Stop and one-shot activation

Keys, signals and original native-epoch checks continue while inspection is pending, with zero mining-worker launches and an explicit PENDING/stop notice. No-children cannot end this pending stage. S records only an in-memory desire to open Settings; after initial decisions, the panel requests a separate fresh UI Load instead of consuming a bootstrap reply. Early Q/Ctrl+C revokes the helper without creating workers. Optional joining remains after worker settlement.

Fleet/source/history/energy/tuning/display-reader startup follows these decisions once, after stop/key processing. Original intent/native ownership is rechecked around caller-owned factories and before each first launch. A callback returning after stop or a synthetic native epoch change cannot start the next source or worker. This is not a hard native/virtual-factory latency guarantee.

## Independent availability and clocks

UI/manual initial reads remain dashboard-only; `--no-market` and `--no-energy` still suppress their respective initial reads. Existing plain-mode electricity behaviour is preserved: the owner is constructed there only for a needed initial Electricity read, never for a live panel bridge.

A rejected configured Manual read is unknown source selection, **not absence**: both manual and requested public references stay withheld. A valid enabled manual declaration plus a public request also withholds both before constructing either source. No public/manual fallback is invented. Failed Electricity inspection authorizes no session/durable collection; forecast startup requiring unknown initial choices is withheld. Other valid inputs retain independent availability. A real service's successful missing-file/default load is distinct from a rejected read. Later explicit live actions keep P-14r's reviewed semantics, never auto-starting a withheld public source or restarting workers.

Saved manual reference/confirmation instants remain exact. Its existing pure display factory receives the original paired supervisor startup UTC/monotonic anchor captured before helper delay, not acknowledgement time; delay cannot renew freshness. Frozen runtime/tariff/collection/source rules remain separate.

## Evidence and remaining gates

The previous actual native supervisor fails the held-read/early-stop worker-count regression: the fake worker had already started before preference inspection. The changed parent passes held initial Load + deferred S + early Q with zero starts, while held live writes/lost acknowledgements still stop one existing worker. A controlled inherited helper explicitly records/releases UI/Manual/Electricity/deferred-UI tickets and proves one worker and no preference writes. Nine actual-helper native cases use synthetic files and memory-only source/fleet probes for plain/CLI behaviour, original declarations/clocks, malformed input, conflict and stop/synthetic epoch denial. Exact full-profile/privacy results belong in the [execution ledger](architecture/execution-plan.md) after completion.

Main pre-run runtime/game/pool/language configuration and idle `settings` remain synchronous. Actual-module lookup, stats-server setup, console output, unrelated native operations and arbitrary source/fleet factories are not isolated or hard-bounded by this change. Complete privacy/vault/retention, qualified power/game/provider/native GPU adapters, physical storage/scheduling and deployment/release remain open. No real miner, device, game, provider/account, trust-store change or release builder is exercised.
