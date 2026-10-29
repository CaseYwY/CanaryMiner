# Isolated persistence consent and private enrollment (P-08g)

This adds a bounded private authorization store, system-random namespace/session provisioning, explicit selected-set enrollment and an injected native settings page to [P-08f capture](energy-capture.md). R-ENG-001, R-PRIV-001/002, R-SAFE-001/002 and R-REL-001 remain partial; M-ENERGY/M-PRIVACY/M-SUPERVISOR; T-CONFIG/T-UI/T-ENERGY/T-INTENT/T-PRIVACY/T-RELEASE; D-41.

**Development integration.** The ordinary miner still leaves the capture factory and authorization access empty. Settings shows unavailable until a caller supplies the authorization owner and explicit selected UUIDs. The fixture exercises the actual native page/helper and hands a separately issued plan to the existing capture bridge. It does not discover GPUs, activate production persistence or install/replace a miner. Production helper discovery/packaging, whole-collector exclusivity, parent-death quarantine and real-cadence capacity qualification remain required.

## Separate saved consent

`energy-persistence.json` is the intended private state file, independent of UI, runtime, manual-market, tariff and session-energy preferences. Missing means persistence **OFF**, with no namespace, enrollment or session identity. Load does not create the file, directory or lock. Malformed, oversized, unsupported or future-dated saved state is unavailable, never replaced by defaults.

The canonical schema-v1 record (`CMEP1`) contains a monotonic revision, last-change UTC instant, independent `persistenceAuthorized` plus capture parameters, all retention budgets, enrollment state, private namespace/binding and the **latest reserved** session identity. A SHA-256 corruption checksum seals the canonical body. It is not authentication against an actor who can rewrite the private directory. There are no raw UUIDs, account names, wallets, rates, forecasts or credited income in this record. Derived scope/channel keys and namespace/session/times remain private correlation data.

Save uses an exact loaded-document hash (or explicit `absent`) under a nonblocking per-file lease. A competing writer cannot silently win over a stale review. Atomic replacement uses an exclusive temporary file, flush and platform persistence calls; POSIX files are 0600 with parent-directory fsync, Windows files inherit the protected directory ACL. Protected installation/state parents are a caller/deployment requirement; lexical path validation does not authenticate parents or hostile reparse races. No backup/temp cleanup or repair is inferred from a failed write.

Capture defaults remain selection 1000 ms, maximum gap 5000 ms, flush 2000 ms, buffer/batch 128. Bounds are 500–60000 ms selection, selection–60000 ms gap, 50–60000 ms flush, 1–128 buffer and 1–buffer batch. They are independent of rendering and in-memory collection. The exact [history/compaction budgets](energy-compaction.md) are all explicitly reviewable; compaction defaults **OFF**. Retention limits become immutable at enrollment, while capture cadence and consent can be changed for the next explicit parent. Changing consent cannot modify an already issued plan or restart mining.

## Identity and enrollment transaction boundary

This first schema binds **one selected set per authorization document**. It has no reset, delete, namespace rotation, new-set adoption or implicit migration command. Full UUID input is bounded to 64 unique devices and uses the existing case-normalized, namespace-salted mapping; reorder preserves binding, membership changes reject. A different set needs a separately reviewed future enrollment workflow/location, not reinterpretation of this record.

Namespaces and native `session.<32 lowercase hex>` IDs use 128 bits from Windows `BCryptGenRandom` with the system-preferred RNG or Linux `getrandom`, entirely inside the helper. Interrupted reads are handled; failures have no timestamp, PID, device, PRNG or fixture fallback. A small uniqueness/shape smoke test does not certify entropy. Only the latest reservation is stored; already used session IDs also remain subject to the ledger's used-session rejection. Clearing a string is not secure erasure.

Enrollment is a deliberate partial-transaction state machine, not a claim that three files commit atomically:

1. **Enroll** accepts the reviewed selection and retention/capture parameters with consent OFF. It generates a private namespace and history lower bound, derives the immutable binding and atomically saves **Pending** before any database side effect.
2. The store name is derived privately as `energy-history.<namespace>.db` beside the authorization document. The helper executes explicit **CreateNew**, including the original empty recovery journal. A present database/journal is not adopted or overwritten.
3. Only an exact empty checkpoint and Empty recovery proof allow the document to become **Ready**, still OFF. A separate Save is required to persist ON.
4. A crash/unknown outcome after reservation leaves Pending. Enroll from Pending always rejects. **Inspect** opens only Existing and changes no observations, journal or authorization record. **Confirm** rechecks the same empty bound store under the configuration CAS and marks Ready; it does not enable consent.
5. If either store is missing, corrupted, mismatched or nonempty, confirmation fails and preserves the reservation and surviving files. A crash before database creation therefore does not permit automatic creation on restart. Inspection may create normal lease files/checkpoint SQLite WAL; compare logical facts rather than claiming every database byte stays stationary.

## Fresh-parent issuance and frozen handoff

**Provision** is separate from all native editing actions. It requires Ready, saved ON, exact selected-set mapping and an Existing bound database/journal inspection. Prepared recovery can receive a fresh identity so the capture bridge can expose its recovery page, but it still blocks sampling until reviewed reconciliation. Divergent/missing stores refuse issuance.

The helper reserves and saves a new session ID before returning a successful issuance receipt. `EnergyAuthorizationOwner` admits at most **one provisioning attempt per instance/explicit parent**. Only that successful live receipt can produce a one-use `takePlan`; Load, last-issued identity, lost acknowledgement, stale revision, CLI `--no-energy`, or stop cannot produce a plan. Another explicit parent must reserve a fresh identity. The returned plan always uses Existing and copies the policy, limits and private binding; later saves cannot mutate it. This is a caller handoff contract, not lifetime-exclusive collection authority or OS-enforced single-parent admission.

## Isolated asynchronous ownership and uncertainty

`canary_energy_authorization_worker` uses existing anonymous local IPC and no shell/network/provider executable. Config records, requests (`CMEPQ1`) and replies (`CMEPA1`) have a separate **24 KiB** bound; replies include the canonical request hash as well as generation/ticket. Shared public JSON and energy IPC limits are unchanged. A bounded request can carry only the enumerated action's meaningful fields; unknown/ignored policy knobs reject.

The background owner has one flight and no unbounded queue. Render, snapshot, admission, reviewed acknowledgement, plan handoff and `requestStop` perform memory work only. Stop cancels/discards pending authority before optional join, is single-start even if requested before start, and revokes late publication. It cannot roll back an already persisted reservation or consent. Clock reversal latches a fault. Unreaped transport faults latch instead of admitting another helper.

A mutating child that started but returned no valid acknowledgement (or storage uncertainty) retains its immutable original request. Only Load is then allowed. A successful explicit Load exposes the current record alongside the retained input; a separate generation/revision-bound acknowledgement clears **in-memory uncertainty only**. It neither retries a save/enrollment nor grants a capture plan. Pending store enrollment still requires the exact Inspect/Confirm sequence. A failed inspection cannot erase earlier uncertainty, and an unknown provisioning attempt stays consumed even after acknowledgement.

These leases serialize individual operations, not the lifetime of an active collector. This slice does not certify live-parent death while a helper survives, global quarantine/reaping, genuine kernel/D-state stalls, power failure or physical disk durability. Existing unrelated synchronous preference stores retain their prior disk-stall qualification gap.

## Native controls

**S → Electricity → Energy persistence / authorization** and **S → Privacy → Energy persistence / authorization** use the existing ASCII renderer. The injected page has explicit ON/OFF, bounded numeric drafting, full policy/retention review, save, cancel/discard and default-hidden private identities. Q/Ctrl+C stops in every context.

- Arrows select; Enter edits/toggles; V/F2 reviews Save; Enter submits the reviewed request.
- E reviews Enroll with consent OFF; I reveals selected/private data; Space acknowledges before Enter submits.
- F loads the current record, or inspects a known Pending store. C reviews Confirm only after an exact empty-store inspection.
- After an unknown request, F loads; A reviews acknowledgement of the original/current state. No retry is performed.
- Esc cancels review/edit or offers discard for a dirty draft; D discards local edits. PageUp/PageDown scroll long reviews/private details.
- Queued and persisted confirmation are distinct. A changed owner revision cancels review, and a changed saved baseline cannot silently rebase an old dirty draft. Enrollment-fixed retention fields are read-only. Saved/effective capture and CLI override are separate labels.

No key on this page provisions a sampling session. Missing callbacks show unavailable rather than a working switch. Opening this page never reads storage on the renderer thread or saves another page's draft.

## Verification

The automated suites cover both platforms' helpers, canonical codecs, maximum selection, immutable binding/retention, system entropy refusal, competing CAS writers, nonempty/orphan preservation, process death at reservation/creation/issuance, fresh-parent handoff, unknown-input inspection, native review races, CLI override, clock/stop guards and an authorized plan's synthetic readings through the existing bridge. Independent Python probes inspect canonical checksums and SQLite facts from separate parent processes; ordinary artifact checks exclude fixture crash/stall/entropy seams.

Executed evidence (2026-10-29):

- Windows/MSVC: **56/56 focused CTest regression**, followed by **8/8** after the final bounded-input guards and **3/3** after admission-publication exception ordering. The latest authorization program passes **1481 checks**. Independent separate-parent/corruption/artifact probes pass **122 checks**.
- Linux/WSL/GCC: **1481 authorization checks** and **124 independent reconstruction checks** (two extra POSIX permission assertions). Own authorization/owner/helper/core code also passes **ASan+UBSan 1481/124**. Warnings are errors. Linux evidence uses direct GCC, not Linux CMake.
- The rebuilt existing capture suite passes **1392** checks; synthetic supervisor **231**. New native authorization PTY **21** checks, prior tariff **16**, manual-market **20**, session-energy **24**, settings and runtime-settings PTY pass. These use a controlling terminal for SIGINT rather than assuming a swallowed control byte means a hang.
- Python discovery **123 tests, seven explicit fixture-dependent skips**; architecture contracts **4/4**. Windows selection also covers legacy energy recovery/compaction, credits/cursors, decimal/oracle, donation, intent/stop, settings and native-build/import contracts.

Initial checks caught GCC test indentation warnings, older menu-count assertions needing the added row, and PTY harness expectations for an overwritten status message and the scope's structured-field index. These were corrected and affected suites rerun. Final code review moved snapshot allocation before admission commit so allocation failure cannot leave a hidden queued write; allocation failure itself was not fault-injected. No timed-out wrapper is presented as a passed run.

All UUIDs/readings/state are synthetic, including the native-origin-shaped fixture. These are not genuine kernel/disk/parent-death, production CUDA, physical-sensor, whole-lifecycle exclusivity or retention-duration qualifications. No real GPU, pool, account, game, electricity plan or production state is accessed.

```text
cmake --preset tests
cmake --build --preset tests --target test_energy_authorization canary_energy_authorization_worker energy_authorization_worker_fixture energy_authorization_fixture canary_energy_history_worker test_pearl_cuda_cli_host
ctest --preset tests -R "^energy_authorization" --output-on-failure
```

Follow-up [P-08h](energy-collector.md) adds cooperative lifetime admission, transaction fences and scoped collector parent-death checks. The evidence above remains the P-08g slice. Next work: bounded ordinary-runtime loading/provisioning, package/trust qualification and explicit selection-change recovery; real-cadence retention planning; monetary bill continuation, qualified credits/prices and today's forecast. Saving finite exact budgets supplies neither a guaranteed retention duration nor a raw archival strategy.
