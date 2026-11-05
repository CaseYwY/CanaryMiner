# Do not reissue a settled Run against an identical permission ceiling (P-03j/P-11p)

M-SUPERVISOR/M-GAME; R-SAFE-001/002, R-GAME-004/005; T-INTENT/T-GAME; D-121.

## Reproduced failure and distinguishing probe

The timing-sensitive cached-safety failure retained in P-09h's historical evidence has now been reproduced twice using
the existing `test_game_worker_session <identity-helper> <detector-fixture> cached-safety` selection, at concurrency four.
The unchanged seven-check case failed the same four assertions at iterations 94 and 31: the child was no longer alive and
drained Paused when the test expected cooperative Pause and continued Pause renewals. No installed miner, GPU or real game
was used. Serial passing runs alone had not resolved that failure.

Three candidates were distinguished: child-side command expiry, a shortened original-evidence ceiling, and asynchronous
pending-command/receipt settlement. A private bounded state-history probe, printed only after failure and without command
bindings, showed a fixed original safety ceiling with repeated Run revisions sharing that same deadline. One such Run was
still pending when early Pause became requested; the parent could only send Pause after its receipt settled, leaving less
delivery time. The owned CPU child then exited with command-expiry code 8. The original safety ceiling had not shrunk or
renewed. Probe instrumentation is removed, not shipped.

## Narrow control rule

In the ordinary, non-economic/non-experiment `GameWorkerSession` path, after native receipt settlement and current
permission checks, a settled Run is not reissued if its deadline **equals** the current permitted Run ceiling. Reissuing
that exact ceiling supplies no new permission and only occupies the single-flight native channel. The original command,
revision, deadline and receipt remain unchanged until a meaningful transition.

This is not a transport timeout extension, larger safety age, extra headroom, forged receipt or weakening of pending-Run
withdrawal. The existing early-Pause rule still applies before the true permission deadline; genuine missing/shorter
permission still revokes an uncovered pending Run, and stop remains terminal. A genuinely new eligible safety observation
may extend the ceiling and still causes renewal. Settled Pause leases continue renewing while no Run permission exists.
Economic/experiment commands, host-wait values, protocol frames, detection/clock/thermal/force-resume rules and donation are
unchanged. No worker restart or permission is created by suppressing a redundant command.

## Regression at the native call site

`game_worker_cached_renewal` starts the actual session, owned identity helper, native process owner and CPU child. It waits
for the real initial Pause and Run receipts to settle. With a fixed original safety ceiling it advances **real time** into
the ordinary renewal window but outside early-Pause headroom, then checks that the exact settled revision/deadline persists
with no pending command. A separate fresh-observation control must still issue a later deadline. Before the fix, this
twelve-check regression failed exactly the cached-reissue assertion twice; after the fix it passes.

An earlier draft advanced the parent to synthetic future time. Its new native command correctly failed the owner's actual
issue-time checks, so that draft was not accepted as a valid positive control. The final test uses actual elapsed time and
does not relax production clock guards. It measures a host fixture, not physical work or a hard scheduler guarantee.

```text
ctest --test-dir <owned-host-build> -R "^game_worker_cached_renewal$" --repeat until-fail:5 --output-on-failure
```

Exact cross-platform, sanitizer, original-concurrency reproduction, full regression and privacy evidence is in the
[execution ledger](architecture/execution-plan.md). Freshly rebuilt own code and existing helper fixtures are identified
separately. Neither a bounded successful stress run nor this no-op rule proves all possible overloaded scheduling, native
driver/kernel stalls, global helper quarantine, real GPU/game safety or production qualification. Those gates remain.
