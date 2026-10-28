// Fee wiring (H-022): the outer-loop lane switch. The scheduler (fee.h) lives here, outside runMiner:
//   * the USER run carries a loop hook; when the scheduler opens a window the hook ends the run at a loop top (never mid-search,
//     shares already settled) with kRunFeeWindow;
//   * runFeeWindow() then runs runMiner again with the same pool settings but wallet = the fee destination and worker "canaryfee",
//     under a hook that ends it with kRunFeeWindowDone when the window is complete or the fee pool did not work;
//   * the user run resumes with its original options. Every runMiner builds its own source and session, so no lane shares one.
// A fee-run failure is debt (<= one window), never a D3 guard restart, never a user transport failure, never a stop.
// Everything touching time, the run, logging and publishing is injected so the CPU tests drive it with fakes.

#pragma once

#include <stdint.h>

#include <functional>
#include <mutex>
#include <string>

#include "../core/run.h"
#include "fee.h"
#include "config.h"
#include "restart_budget.h"

namespace om {

constexpr const char *kFeeWorker = CANARY_FEE_POOL_WORKER;

/** The developer donation runs only when the user left it on (the default) and this build has a destination. When false no
 *  scheduler, no window and no state file exist. */
inline bool donationActive(const ProductConfig &cfg) { return cfg.donation && feeDestinationConfigured(); }

/** The policy the product uses. A failed window costs the user a run restart (reconnect + re-prepare), so a failing fee pool is
 *  retried every 10 minutes, not every 30 seconds; the unmet time stays debt (capped at one window) meanwhile. */
inline FeePolicy productFeePolicy() {
    FeePolicy p;
#ifdef CANARY_FEE_TEST_POLICY
    p.cycleMs = 10ull * 60 * 1000;
    p.windowMs = 60ull * 1000;
    p.retryBackoffMs = 2ull * 60 * 1000;
#else
    p.retryBackoffMs = 10ull * 60 * 1000;
#endif
    return p;
}

/** What the stats endpoint shows about the fee (user counters never include fee shares). */
struct FeeStatus {
    bool configured = false;
    bool windowActive = false;
    uint64_t cycle = 0;
    uint64_t userMs = 0, feeMs = 0, debtMs = 0;  // this cycle
    uint64_t poolFailures = 0;
    uint64_t windowsCompleted = 0;
};

struct FeeLoopDeps {
    std::function<uint64_t()> nowMs;                          // monotonic
    std::function<int(const RunOptions &)> run;               // runMiner for this device
    std::function<bool()> stopped;                            // operator stop requested
    std::function<void(uint64_t)> sleepMs;                    // may return early when stopped
    std::function<void(const std::string &)> log;             // one line, no newline
    std::function<void(const FeeStatus &)> publish;           // optional
    std::function<void()> userRunSuspended;                   // optional: the user run ended for a fee window (fold its counters)
    RunObserver *feeObserver = nullptr;                       // optional: receives the fee run's report intervals
    std::function<void(bool)> feeWindowBoundary;              // optional: true = window opens, false = closed (fee counters fold)
};

class FeeSession {
   public:
    static constexpr uint64_t kSaveEveryMs = 30 * 1000;
    static constexpr uint64_t kStartTimeoutMs = 60 * 1000;  // a fee run with no productive iteration for this long has failed

    FeeSession(const FeePolicy &policy, FeeRandom *rng, const FeeStateFile &file, const std::string &destination, FeeLoopDeps deps)
        : sched_(policy, rng), file_(file), destination_(destination), deps_(std::move(deps)) {}

    bool enabled() const { return !destination_.empty(); }
    const FeeScheduler &scheduler() const { return sched_; }
    uint64_t poolFailures() const { return poolFailures_; }
    uint64_t windowsCompleted() const { return windowsCompleted_; }

    /** Load the persisted state. A damaged file starts a fresh cycle with a warning; it never blocks mining or skips the fee. */
    void load() {
        std::lock_guard<std::recursive_mutex> lock(mu_);
        FeeState st;
        const auto r = file_.load(&st);
        if (r == FeeStateFile::LoadResult::Corrupt) {
            deps_.log("warning: donation-state.json is damaged and was ignored; starting a fresh donation cycle");
            sched_.restore(FeeState{});
        } else if (r == FeeStateFile::LoadResult::LoadedBackup) {
            deps_.log("warning: donation-state.json was damaged; restored from the backup copy");
            sched_.restore(st);
        } else {
            sched_.restore(st);  // Fresh: uninitialised state starts cycle 0
        }
        publish(false);
    }

    /** Persist now (also marks the scheduler clean). A write failure is logged once and never stops mining. */
    void save() {
        std::lock_guard<std::recursive_mutex> lock(mu_);
        std::string err;
        if (file_.save(sched_.state(), &err)) {
            sched_.markSaved();
            saveFailureLogged_ = false;
        } else if (!saveFailureLogged_) {
            deps_.log("warning: cannot save fee state: " + err);
            saveFailureLogged_ = true;
        }
        lastSaveMs_ = deps_.nowMs();
    }

    /** Shutdown flush (any thread): saves the donation state unconditionally when the fee is enabled, under the same lock as the
     *  run thread's hooks, so the file is a consistent snapshot even when the run loop is still winding down. */
    void flushForShutdown() {
        if (!enabled()) return;
        std::lock_guard<std::recursive_mutex> lock(mu_);
        save();
    }

    FeeStatus status(bool windowActive) const {
        std::lock_guard<std::recursive_mutex> lock(mu_);
        FeeStatus s;
        s.configured = enabled();
        s.windowActive = windowActive;
        s.cycle = sched_.state().cycle;
        s.userMs = sched_.state().userMs;
        s.feeMs = sched_.state().feeMs;
        s.debtMs = sched_.state().debtMs;
        s.poolFailures = poolFailures_;
        s.windowsCompleted = windowsCompleted_;
        return s;
    }

    // ---- the user run's hook
    class UserHook final : public RunLoopHook {
       public:
        explicit UserHook(FeeSession *s) : s_(s) {}
        int atLoopTop(bool searchCompleted) override { return s_->userTop(searchCompleted); }

       private:
        FeeSession *s_;
    };

    // ---- the fee run's hook
    class WindowHook final : public RunLoopHook {
       public:
        explicit WindowHook(FeeSession *s) : s_(s) {}
        int atLoopTop(bool searchCompleted) override { return s_->windowTop(searchCompleted); }

       private:
        FeeSession *s_;
    };

    RunLoopHook *userHook() { return &userHook_; }

    /** The scheduler asked for a window and the user run has returned kRunFeeWindow. Runs the whole window. Returns the fee run's
     *  exit code only when it was the operator stop (0); otherwise the user run is meant to resume. */
    int runFeeWindow(const RunOptions &userOpt) {
        const uint64_t t0 = deps_.nowMs();
        std::unique_lock<std::recursive_mutex> lock(mu_);
        // Setup, connect and prepare are not productive: stop the clock, keep the window open.
        sched_.advance(t0, false, true, true);
        windowStartMs_ = t0;
        lastProductiveMs_ = t0;
        firstProductiveMs_ = 0;
        feeMsAtStart_ = sched_.state().feeMs;
        windowFailed_ = false;
        failReason_.clear();
        const uint64_t owed = sched_.windowRemainingMs();
        deps_.log("donation window: opening, " + std::to_string(owed / 1000) + " s to mine on " + destination_ + " (cycle " +
                  std::to_string(sched_.state().cycle) + ")");
        save();
        if (deps_.userRunSuspended) deps_.userRunSuspended();
        if (deps_.feeWindowBoundary) deps_.feeWindowBoundary(true);

        RunOptions feeOpt = userOpt;
        feeOpt.wallet = destination_;
        feeOpt.worker = kFeeWorker;
        feeOpt.poolHost = CANARY_FEE_POOL_HOST;
        feeOpt.poolPort = CANARY_FEE_POOL_PORT;
        feeOpt.poolProofCompression = poolPresetSpeaksV2(CANARY_FEE_POOL_PRESET);
        feeOpt.poolAccount = CANARY_FEE_POOL_ACCOUNT;
        feeOpt.pearlPolicy.network = PearlNetwork::Mainnet;
        feeOpt.pearlPolicy.targetSemantics = PearlTargetSemantics::BaseBeforePenalty;
        feeOpt.observer = deps_.feeObserver;
        WindowHook hook(this);
        feeOpt.loopHook = &hook;
        inWindow_ = true;
        publish(true);
        lock.unlock();  // the run below calls the hooks, which take the lock; never hold it across the run
        deps_.log("lane start: donation window");
        const int rc = deps_.run(feeOpt);
        deps_.log("lane end: donation window rc=" + std::to_string(rc));
        lock.lock();
        inWindow_ = false;
        const uint64_t t1 = deps_.nowMs();
        sched_.advance(t1, false, true, true);  // credit the last stretch to the window it belonged to, then stop the clock

        std::string result;
        bool stop = false;
        if (rc == kRunFeeWindowDone && !windowFailed_) {
            result = "ok";
            ++windowsCompleted_;
        } else if (rc == 0 || (deps_.stopped && deps_.stopped())) {
            result = "interrupted by stop";
            stop = true;
        } else {
            result = "failed (" + (windowFailed_ ? failReason_ : std::string("fee run ended with code ") + std::to_string(rc)) + ")";
            sched_.noteFeePoolFailure(t1);
            ++poolFailures_;
        }
        // Back to user mode (a no-op after a failure or a completed window); the unmet part is carried as debt by the scheduler.
        sched_.advance(t1, false, true);
        if (deps_.feeWindowBoundary) deps_.feeWindowBoundary(false);
        const uint64_t mined = sched_.state().feeMs - std::min(sched_.state().feeMs, feeMsAtStart_);
        std::string line = "donation window: closed after " + std::to_string((t1 - t0) / 1000) + " s, mined " + std::to_string(mined / 1000) +
                           " s, result " + result;
        if (firstProductiveMs_) line += ", switch-in " + std::to_string((firstProductiveMs_ - t0) / 1000) + " s";
        deps_.log(line);
        save();
        publish(false);
        return stop ? 0 : -1;
    }

    /** Called when the user run (or the whole session) ends: keeps the unmet window as debt on disk. */
    void finish() {
        if (!enabled()) return;
        std::lock_guard<std::recursive_mutex> lock(mu_);
        sched_.advance(deps_.nowMs(), false, true);
        save();
    }

    void publish(bool windowActive) {
        if (deps_.publish) deps_.publish(status(windowActive));
    }

   private:
    mutable std::recursive_mutex mu_;  // guards sched_ and the counters: the run thread's hooks, the window code, and the shutdown flush
    FeeScheduler sched_;
    FeeStateFile file_;
    std::string destination_;
    FeeLoopDeps deps_;
    UserHook userHook_{this};
    uint64_t poolFailures_ = 0, windowsCompleted_ = 0;
    uint64_t lastSaveMs_ = 0;
    bool saveFailureLogged_ = false;
    bool inWindow_ = false, windowFailed_ = false;
    std::string failReason_;
    uint64_t windowStartMs_ = 0, lastProductiveMs_ = 0, firstProductiveMs_ = 0, feeMsAtStart_ = 0;

    void maybeSave(uint64_t now) {
        if (sched_.changedSinceSave() && now - lastSaveMs_ >= kSaveEveryMs) save();
    }

    int userTop(bool searchCompleted) {
        std::lock_guard<std::recursive_mutex> lock(mu_);
        const uint64_t now = deps_.nowMs();
        const FeeMode m = sched_.advance(now, searchCompleted, true);
        if (m == FeeMode::Fee) return kRunFeeWindow;
        maybeSave(now);
        publish(false);
        return 0;
    }

    int windowTop(bool searchCompleted) {
        std::lock_guard<std::recursive_mutex> lock(mu_);
        const uint64_t now = deps_.nowMs();
        if (searchCompleted) {
            lastProductiveMs_ = now;
            if (!firstProductiveMs_) firstProductiveMs_ = now;
        }
        if (deps_.stopped && deps_.stopped()) return 0;  // runMiner's own loop condition ends the run
        const FeeMode m = sched_.advance(now, searchCompleted, true, true);
        if (m != FeeMode::Fee) return kRunFeeWindowDone;  // window complete
        if (now - lastProductiveMs_ >= kStartTimeoutMs) {
            windowFailed_ = true;
            failReason_ = firstProductiveMs_ ? "no productive work for 60 s" : "fee pool did not provide work within 60 s";
            return kRunFeeWindowDone;
        }
        maybeSave(now);
        publish(true);
        return 0;
    }
};

/**
 * The outer loop: user runs, fee windows and D3 guard restarts. Returns the process-level run code.
 *   * kRunFeeWindow       -> run the window, resume the user run (does not touch the D3 budget);
 *   * kRunGuardStop       -> D3: restart after 10 s, at most 3 per rolling hour (unchanged behaviour);
 *   * anything else       -> returned.
 * `fee` may be null (no destination compiled in): then this is exactly the D3 loop.
 */
inline int runOuterLoop(const RunOptions &userOpt, FeeSession *fee, FeeLoopDeps &deps, GuardRestartBudget *budget, int exitOk, int exitRuntime) {
    int rc = 0;
    for (;;) {
        if (deps.stopped && deps.stopped()) { rc = exitOk; break; }
        RunOptions o = userOpt;
        if (fee && fee->enabled()) o.loopHook = fee->userHook();
        deps.log("lane start: user");
        rc = deps.run(o);
        deps.log("lane end: user rc=" + std::to_string(rc));
        if (deps.stopped && deps.stopped()) { rc = exitOk; break; }
        if (fee && fee->enabled() && rc == kRunFeeWindow) {
            if (deps.stopped && deps.stopped()) { rc = exitOk; break; }
            if (fee->runFeeWindow(userOpt) == 0) { rc = exitOk; break; }
            continue;
        }
        if (rc != kRunGuardStop) break;
        if (deps.stopped && deps.stopped()) { rc = exitOk; break; }
        const uint64_t nowMs = deps.nowMs();
        if (!budget->admit(nowMs)) {
            deps.log("guard stop: restart budget spent (3 per hour); stopping");
            rc = exitRuntime;
            break;
        }
        deps.log("guard stop: restarting this GPU in 10 s (" + std::to_string(budget->usedInWindow(nowMs)) + " of 3 this hour)");
        for (uint64_t waited = 0; waited < GuardRestartBudget::kDelayMs && !(deps.stopped && deps.stopped()); waited += 100) deps.sleepMs(100);
        if (deps.stopped && deps.stopped()) { rc = exitOk; break; }
    }
    if (fee) fee->finish();
    return rc;
}

}  // namespace om
