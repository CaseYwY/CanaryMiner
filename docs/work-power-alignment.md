# Original work-boundary / power alignment (P-09f)

M-PROFILES/M-SUPERVISOR; R-PRO-001/002/003, R-SAFE-001/002 and R-PRIV-002; T-PROFILES/T-INTENT/T-PRIVACY; D-88.

The optional [completed-work sink](completed-work-capture.md) now receives explicit no-work boundary checkpoints. `CompletedWorkCapture` can retain original constant-counter intervals, and `alignWorkPower` can join an interval to an independently timestamped raw power reading. Both originals survive unchanged. The result is historical raw evidence, **not** a qualified `WorkProfileSample`, a command permit, a sampler or source-liveness proof.

## Actual loop ordering

`pauseAtWorkBoundary` has an optional observation callback. A Paused observation follows the successful lookahead drain and the actual control's `pauseBoundary(true)`; repeated observations follow `pauseHeld()`. A Leaving observation precedes resume notification. Callback refusal ends that opted-in run. Existing callers use the unchanged non-observing overload.

The shared loop also closes an interval before entering pause/drain handling, before `applyWorkWait`, before initial configuration and before fetching/preparing/searching again. Runnable checkpoints follow installation of the actual wait. The interval around the ordinary complete-batch host wait therefore retains the installed value, including explicit zero. No interval crosses a setting change or preparation/search. The callback remains bounded, memory-only and worker-thread-owned; it performs no sensor read, IPC or file access. It introduces no extra sleep, wait duration, GPU action or fee-policy change.

Callbacks use current original UTC/monotonic clocks at the actual boundary. They never reuse or retime `lastBatch`. Before configuration, an actual drained initial Pause can supply a zero-work baseline interval. An idle/paused checkpoint does not independently certify that the latest command is still authorized: the enclosing native coordinator must bind actual original settlements and current intent separately.

## Constant-counter intervals

The existing capture's `boundarySourceKey` is empty by default. Supplying a private provisioned 32-character lowercase hex key explicitly enables intervals; it must be bound to the exact worker incarnation by the future native coordinator. It is not a wallet, device ordinal, display worker name or automatic identity discovery. The existing default-off capture/parent token remains required.

An interval retains source key, parent generation, counter epoch, run/lane, interval ordinal, the two adjacent boundary-event ordinals, complete batch/attempt counts, actual installed wait, both original clocks and both exact TMAC totals. Totals are constant throughout the bracket, so no work interpolation is performed. A checkpoint with unchanged boundary kind/wait advances an interval; Leaving closes it. Any kind/wait change without Leaving or completed batch while a boundary is still open latches discontinuity. End/stop/fault closes the open state without extending a retained interval. Sequential fee/user run calls cannot bridge it or relabel an old original.

Only the latest interval is retained in this worker-thread snapshot; reading it is not a new observation. Original sequence numbers expose advancement but this is not a lossless queue. The later transport/capture owner must select, bound and account for delivery, gaps and source sequence consumption. The accumulator itself retains constant memory rather than an unbounded per-batch archive. All reads remain on the worker thread.

## Preserve source support, not just delivery time

`WorkPowerReading` preserves its own source sequence, exact work-source/parent/counter identity, full power basis/origin/scope/adapter, original observation instant, read-begin/read-end, optional milliwatts and optional source measurement-support interval. The support includes any averaging or cached measurement window. A successful sensor API call alone does **not** establish that support; missing support or power is unavailable for this alignment. Explicit measured zero is valid.

The independently default-off alignment policy freezes expected identity/evidence and separate maximum bracket, read duration and source age, each positive and at most 60 seconds. It accepts raw SyntheticFixture or ObservedTelemetry provenance; forecasts are refused. That acceptance does not qualify a real adapter or change the profile module's SyntheticFixture-only admission.

The complete read interval, complete source support and original observation instant must fall **strictly inside** one constant-counter work interval on both clocks. Same-millisecond edge ties are ambiguous and refused. Support contains the observation and ends no later than read completion. A cached sensor window extending across work is rejected even if its API read occurred during Pause. Clock rollback, inconsistent elapsed dual clocks, future evidence, mismatched source/basis/epoch, malformed intervals and excessive bracket/read duration refuse. Source-age expiry is measured from the original observation, never receipt/queue time.

The resulting immutable `AlignedWorkPower` copies both originals. It creates no synthetic point at delivery time, fills no gap, infers no safety/game/drift status and performs no energy interpolation. A caller can inspect historical alignment after source stop; that is not fresh authority. Native source sequence admission, lifetime binding, current source status, original command receipts, qualified device/sensor support, and independent safety/game/drift evidence remain required before measurement admission.

This conservative bracket can yield no aligned readings on a continuously busy worker or at zero host wait. It does not lengthen pauses/waits to manufacture coverage. Idle-supported power alone must not stand in for energy consumed during running intervals; complete independently timed power-stream integration and coverage are still necessary for an actual work/energy profile.

## Verification scope

The new test uses the actual accumulator and shared pause helper with explicit synthetic dual clocks and power support. It covers initial zero-work Pause, adjacent event ordering, original preservation, both lanes, an explicit zero-wait change, forbidden open-boundary work, unsupported identities/provenance, missing read/support/power, legitimate zero power, every support/read edge tie, cached-support overlap, exact duration/age ceilings and clock mismatch. Actual shared-loop CPU tests exercise initial held checkpoints before configuration, unchanged completed-work originals, callback-failure shutdown and boundary closure before preparation/search. The self-owned native IPC peer now enables these raw callbacks too, checking synchronized proof/count ordering and setup/search exclusion.

This slice does not enable a native power sampler or sample wire protocol, change the 384-byte command frame, construct an experiment in `GameWorkerSession`, qualify physical NVML averaging/support, or activate ordinary CLI optimization. The pinned CUDA/SASS/protocol and integrated physical GPU/game gates remain pending. Exact CPU/native-process results are recorded below and in the execution ledger.

Windows focused CMake/CTest **23/23** passes. Windows/Linux ordinary and Linux **ASan+UBSan with leak detection** pass new alignment **242**, exact work **8,291**, actual loop/source **72**, pause/installed-wait **62**, pause helper **31**, fee-session **205**, command protocol **818** and self-owned native IPC **132** checks. Both Linux modes rebuild changed sources, fixture/ordinary loop artifacts, pause/fee/protocol programs; the unchanged native process harness and unrelated support objects are reused from their matching ordinary/instrumented builds. Ordinary admission and fixture-isolation guards pass. Windows additionally covers capture/experiment/budget/native handoff, profile, worker lifecycle/pipes, donation/intent/throttle, Pearl run-session/host CLI, recipe and architecture. Final architecture/import and diff checks pass. Linux evidence is direct strict GCC, not a Linux CMake or production CUDA build.
