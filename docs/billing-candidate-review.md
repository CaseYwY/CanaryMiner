# Native candidate preview and explicit submission (P-05s)

M-TARIFF/M-ENERGY; R-UI-007/008, R-TAR-002–005, R-ENG-001, R-SAFE-001/002, R-PRIV-002 and R-REL-001; T-TARIFF/T-ENERGY/T-INTENT/T-PRIVACY; D-58. This extends the existing [native billing review](billing-review.md) with the [readonly compute owner's](billing-compute.md) exact original and hypothetical next prefix. Ordinary settings/supervisor access remains empty.

## Native controls and private presentation

Within **S → Electricity / Privacy → Billing originals / recovery review**, **P** switches between monetary recovery and candidate preview. Each view has independent complete-document construction and the same I reveal/hide, scrolling, T/B first/last, Esc cancel/back and Q/Ctrl+C stop behavior. Private source paths, binding, rates, amounts and times remain hidden until explicitly revealed. Candidate failure/oversize does not block inspecting the recovery document.

The candidate page labels its output **HYPOTHETICAL ONLY**, distinguishes reviewable/current-delivered-version from historical/unavailable, and keeps last source coverage and calculation status visible without exposing values. A preserved historical candidate remains readable but cannot be submitted. Its complete document includes:

- Compute and monetary generation/revision, the original query time and separately labelled scheduling hint.
- Currency, exact provisional change and total, hypothetical settled total, settlement-dependence, and every field of the hypothetical ending register.
- The full frozen monetary binding, prior committed/pending original, staged original/certificate and historical stage outcome that supplied the review basis.
- The complete proposed original, trace and nested source/ancestry/coverage receipt, followed by an explicit complete-candidate end marker.

Provisional change is not incremental mining cost; even a calculated settled total remains hypothetical until independently confirmed. Neither preview is actual income, profit, a payable utility bill or a post-write current observation. The original 16-MiB/131072-line and nested bounds, reversible ASCII byte representation and viewport caching apply. Missing/over-budget documents cannot acknowledge or send.

**V** opens Submit review only when both owners' exact snapshots match. **I**, then **Space**, acknowledges the complete original and preview; **Enter/F2** attempts one explicit Submit. Hiding values or changing either observed version clears acknowledgement. Repeat keys cannot resend an accepted monetary snapshot. Esc cancels without staging input. Other monetary recovery keys retain their previous behavior on the recovery page.

**F** explicitly inspects monetary facts. **G** explicitly recomputes against the latest delivered monetary observation using the last bounded admitted notification. A new observation preserves that hint solely for a new explicit read; it starts no work. Deduplication is scoped to the monetary revision, and explicit invalidate still clears the hint. If the prior hint ends at/before the now-committed prefix, recompute refuses rather than extending power or replaying the prior candidate.

## Final dual-version admission

`billingCandidateReview` borrows both owners. Its memory-only snapshot callback delivers the latest monetary observation to compute and returns the immutable compute snapshot; no filesystem or helper I/O occurs on the input/render path. An interleaving observation can temporarily make the pair unavailable, never falsely matched.

Submit first matches both requested generation/revision pairs and the exact frozen monetary snapshot pointer. `BillingOwner::submitReviewed` retains its one borrowed-input slot, bounded original validation/canonical reconstruction outside the stop mutex and final monetary revision check. Only then, under the monetary admission mutex, the optional memory-only claim calls `BillingComputeOwner::claimReviewed` to check and consume the exact compute review once. Lock order is **monetary admission → compute claim**; compute never calls back into the monetary owner. The claim must not perform I/O, block, or re-enter the monetary owner.

This is the handoff's linearization boundary: a compute observation/invalidation/recompute/stop before claim rejects the send; after claim, the exact already-reviewed original belongs to the explicit monetary operation. Monetary changes cannot interleave between its final check and enqueue. A consumed claim is not a durable acknowledgement; an allocation/admission failure after consumption requires a new explicit inspection/recompute/review, not restoration of the old authority.

The queued monetary owner still performs its full stage/money/stage preflight and the helper's live CAS before applying anything. Candidate proof cannot bypass those checks, reopen an uncertain stage, or update its receipt's times. Current monetary read, retained earlier read, historical action proof and unknown handoff intent keep their original meanings. A later refresh is required to obtain current facts.

The borrowed adapter's stop callback revokes the monetary owner and then compute. Existing settings/supervisor stop paths invoke it even when the panel is closed; joins remain the owning caller's responsibility. There is no automatic source notification wiring, production enrollment or financial activation in this slice.

## Verification and follow-up

Tests exercise full private preview/receipt rendering, maximum binding and oversize refusal, both stale-version directions, invalid input before the final claim, invalidation exactly at claim, one-use concurrent handoff, duplicate keys, cancellation, recompute on a new monetary review, separate confirmation/current-read semantics and both-owner stop. Independent controlling-terminal fixtures cover full-tail navigation, resizing, cancellation, acknowledged Ctrl+C, explicit recompute and one Submit.

Executed evidence:

- Windows/MSVC focused **8/8** plus native integration/build/architecture **4/4**. Both platforms pass candidate review **175**, existing recovery review **850**, owner **139**, compute **144**; Windows additionally reruns owner/compute isolated latch and parent lifecycle **10/14/33/7**.
- Linux/WSL/direct GCC candidate/recovery PTY **17/17**, existing settings PTY, settings **155** and synthetic supervisor **259** pass; Windows settings/supervisor **155/275**. The ordinary host CLI and native fixture were rebuilt on both platforms.
- New candidate review and affected recovery review/owner/compute pass **ASan+UBSan 175/850/139/144**, reusing unchanged already-instrumented dependencies. Architecture and Python import guards pass. A test helper initially collided with the C library's `clock` name; it was renamed before runtime validation.

Optional runtime source/cycle/reporting-clock wiring, archive/selected-set/staged-orphan management, real adapters and deployment qualification remain open. The fixture paths and monetary values are synthetic; these host/WSL tests do not qualify real GPU power, suppliers, credits, native Unicode/RTL, production loading or physical disk/kernel stalls.
