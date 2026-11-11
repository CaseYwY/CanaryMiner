# Background idle settings (P-14u)

M-CONFIG/M-UI/M-PRIVACY; R-UI-003/004/007, R-SAFE-001/002, R-PRIV-001/002; T-CONFIG/T-UI/T-INTENT/T-PRIVACY; D-178.

The explicit idle `settings` command now shares the [background preference owner](preferences-owner.md) and [native receipt/private-review bridge](live-preferences.md). `settings_command.h` holds command startup separately from the panel/renderer. There is no direct Store or synchronous file callback in the command.

## Admission, completion and stop

TTY and preexisting stop checks precede owner/module setup. One explicit idle lifetime freezes the same eight configured targets; the fixed helper comes from the actual executable module sibling, never argv, environment, PATH or the current directory. Native command/review consumers link the owner, not the helper disk service, and build beside the helper.

Initial Init/readiness and one exact UI Load precede editing and optional runtime callbacks. Pending inspection permits Q/Ctrl+C/Esc but no Save or speculative editable baseline. Missing helper or rejected UI returns a generic failure, preserves originals and never falls back to synchronous I/O or automatic retry. Explicit stop creates no worker/source and returns cleanly.

All eight later domain Load/Save paths use the existing bridge: queue admission is NOT Saved, matching confirmed receipts alone publish baselines, and original submission clocks/CLI/frozen-session distinctions remain. Idle manual declarations need no running display projector and never activate a market source. A helper-side failed Boolean Save conservatively stays UNKNOWN, including stale-writer/same-plan-key refusals; default-hidden complete original/current inspection replaces raw helper errors or guessed rollback. Explicit acknowledgement establishes a new baseline, not proof/replay of the old outcome.

Stop revokes the idle parent and preference owner before borrowed review/optional runtime settlement and the optional helper join. The command never constructs a mining fleet. Callback exceptions do not become successful saves; current native-parent loss is not reported as a successful operation. Arbitrary callbacks/console/native waits are not hard-bounded here.

## Evidence and remaining gates

The old actual inert CLI fails the held-helper initial-read probe by bypassing that helper. The changed parent passes six actual idle PTY scenarios: observed held Load with Q/SIGINT, observed held Save, actual committed/lost acknowledgement, missing helper and malformed private original. Assertions require no worker/device setup, prompt stop, no speculative Saved, hidden private documents and unchanged unrelated files. Existing eight-domain positive/edit/discard/frozen-session scenarios retain original-file preservation; conflict expectations now follow conservative UNKNOWN rather than raw store errors. Completed ordinary/instrumented rebuilds, repeated PTYs, the Windows 464-case run, exact source digest, discovered timing correction and explicit exclusions are recorded in the [execution ledger](architecture/execution-plan.md).

Main pre-call product configuration/language remain synchronous. Console output, arbitrary callbacks, native scheduling/reaping, crash-persistent unknown originals, complete privacy/vault/retention and actual source/GPU/game/physical/deployment/release qualification remain open. No real miner, hardware probe, game, provider/account, trust-store change or production release builder is run.
