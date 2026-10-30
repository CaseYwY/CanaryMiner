# Game-episode pause and ten-minute profit lock (P-11a)

M-GAME; R-GAME-001/002/004/005/006, R-SAFE-001/002 and R-PRIV-002; T-GAME/T-INTENT; D-62. These memory-only contracts implement game-session decisions and an exact synthetic economic window. They do not discover processes, gather frame metrics, inject into a game, authorize real coexistence or operate a miner. Game policy defaults **Off**; Pause and qualified Coexist remain distinct.

## User-requested ten-minute rule

Within an explicitly started, experience-qualified coexistence episode, the guard starts one **600,000-ms real-time** clock at the first safe coexistence admission. Donation, transitions and temporary safety/detection pauses do not restart or extend this clock. Before the initial ten minutes, the economic rule does not independently grant or remove experience/safety permission.

From the ten-minute boundary onward, continuing coexistence requires a fresh **complete ten-minute window** of estimated game-mode net return. Subsequent evidence uses complete rolling ten-minute windows, not an ever-growing average that can hide a new loss. Mean estimated net is the exact sum of interval revenue minus interval cost, divided by actual total elapsed time. Unequal intervals are not equally weighted.

If this complete window is **zero or negative**, `profitLocked` latches for the current game episode. Positive later data, price improvement, repeated callbacks, unknown detection or a changed process/episode identity cannot unlock it. Mining must remain paused until the selected game's exit has been confirmed. “Pause” means stop mining work; it does not mean shut down the PC, terminate the game, reboot or reset a driver.

Missing, partial, stale, misattributed or unsupported data is **ProfitUnavailable**, not a fabricated zero. It withholds coexistence eligibility without claiming a proven loss; fresh valid positive data may restore eligibility if no loss latch has ever been set. A proved nonpositive window stays locked. This distinction preserves the existing unavailable-versus-zero financial contract.

After confirmed game exit, normal-mode **eligibility** returns; normal economic, thermal and explicit-session rules still apply. User stop is terminal and game exit cannot restart the stopped session.

## Exact window and current qualification boundary

`GameProfitWindow` is immutable and accepts only explicitly synthetic worker-attributed **GPU-board proxy** fixtures. It binds game/profile/episode identity, the complete credit source contract, power scope/adapter, session generation and proof revision. Every interval carries a bounded fixture model-receipt identity and optional exact expected-revenue/estimated-cost amounts in USD. Values are amounts for the whole interval, not rates; missing either amount rejects the full window. Signed cost and explicit complete zero are valid.

Coverage must be contiguous over exactly 600,000 ms, with matching UTC/monotonic duration and at most 4096 intervals. Exact rational addition preserves nonterminating fractions and fails closed at arithmetic capacity. Results retain all inputs, total revenue, cost, net and net-per-real-second. These are estimated production references, not actual income, dollar receipts, utility bills, whole-PC/incremental-wall profit or statistical confidence. Arbitrary real source values cannot be admitted by changing a display label.

The fixture receipt namespace is an offline arithmetic boundary, not real source authentication. A future real adapter must derive these amounts from attributable completed work/rewards, qualified price/FX evidence and matching power/cost trajectories, including donation/transition/pause costs without duplication. Real game baselines and matched incremental-wall adapters remain unqualified. `SyntheticFixture` coexistence permission is for deterministic tests only; ordinary unsupported qualification returns NeedsQualification.

## Detection, episode and clock rules

The guard borrows one explicit `SessionIntent` token. Each detection observation has the same session generation, monotone revision, its own observation clocks, selected game identity and stable episode identity. The caller supplies a complete fresh Present/Absent/Unknown observation; no launcher name alone establishes experience qualification.

Both detection clocks are age-checked. Older observations, same-revision substitutions, future timestamps and wrong identities withhold eligibility. Cached absence cannot advance exit debounce: only observation time from fresh compatible observations counts. Unknown data resets the absence interval. A detection gap exceeding the configured bound restarts absence confirmation, so a long silence cannot prove a continuous exit. Defaults are 2-second exit debounce and 2-second maximum detection age/gap, bounded to 30 seconds; zero exit debounce is supported.

An active episode identity is retained until confirmed absence. Identity churn while still reporting Present conservatively pauses; it cannot erase a loss latch. A subsequent new episode starts a new ten-minute clock only after the old episode's confirmed exit.

Profit evidence must match the exact episode/source/profile/power binding and session generation, map to the episode's frozen UTC/monotonic origin, begin no earlier than coexistence, end no later than now, and be fresh on both clocks. Default maximum age is 10 seconds, bounded to 60 seconds. Lower proof revisions/end times are rejected; a same-revision alternative object is not a replacement proof. Retained stale evidence remains historical. A higher revision may provide a corrected/new complete window, but cannot reverse a prior loss latch.

Faults, UTC/monotonic reversal or cumulative startup clock disagreement over two seconds latch terminal refusal. Missing/unsafe thermal telemetry temporarily cools without resetting the episode's economic timer. Stop precedes every other state and remains terminal across later session epochs. The guard is noncopyable and single-owner; its future owner must retain terminal/episode locks across object replacement and must actually apply/reap mining transitions.

## Verification and remaining work

Host tests cover unequal-duration sign reversal, nonterminating exact amounts, complete zero/signed costs, maximum interval bounds and missing/unsupported input. Episode tests cover the exact 10-minute boundary, later positive data after a loss, rolling evidence, unavailable/stale distinction, cached/forged absence, detection gaps/identity churn, confirmed exit/new episode, thermal pauses, source/time mapping, proof revisions, clock faults and user stop.

Executed evidence: Windows/MSVC focused **3/3**, both platforms **113** checks, also Linux/WSL/direct GCC new implementation/test **ASan+UBSan** with unchanged instrumented credit/arithmetic/transport dependencies. Architecture guards pass. Initial GCC test indentation diagnostics were corrected; the first Linux link omitted the existing bounded-JSON/transport objects and was corrected before runtime validation. Final review added exact rational-capacity, policy-bound and fresh detection-gap cases.

[P-11b](game-detection.md) supplies the isolated native detector and [P-11c](game-runtime.md) retains a fixed detector/guard lifetime. [P-11g](game-worker-session.md) adds optional lifetime supervisor admission and evidence-bound cooperative worker control. Native policy/target/threshold review, qualified safety, latency isolation, CLI binding, permitted game metrics and real baseline qualification, source-qualified economics and durable cross-parent revocation remain separate work. The tests do not run a game or production miner.
