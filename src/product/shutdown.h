// Shutdown state machine:  RUNNING -> DRAINING -> PERSIST -> CLOSE -> RELEASE  (-> DONE)
//
//   RUNNING   mining. A stop request (SIGINT/SIGTERM, Windows console event) moves to DRAINING.
//   DRAINING  the run loop finishes the work it already owns and any share whose reply is still awaited. This waits at most
//             `drainMs`. Past that, every share still without an outcome is recorded as UNKNOWN (never accepted, never
//             replayed - there is no replay path) and the shutdown is marked forced.
//   PERSIST   durable state is written: fee state, the shutdown record (reason, forced, unknown count, final outcome counts).
//   CLOSE     external endpoints and sinks are closed: the stats listener, the log files.
//   RELEASE   device and platform resources are released.
//
// The machine owns no resources and no threads. The caller supplies hooks and drives step() with a monotonic millisecond clock
// (a watcher thread in the miner, a fake clock in the tests). Each step makes at most one transition, so the trace is exact.

#pragma once

#include <stdint.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace om {

enum class ShutdownState { Running, Draining, Persist, Close, Release, Done };

inline const char *shutdownStateName(ShutdownState s) {
    switch (s) {
        case ShutdownState::Running: return "RUNNING";
        case ShutdownState::Draining: return "DRAINING";
        case ShutdownState::Persist: return "PERSIST";
        case ShutdownState::Close: return "CLOSE";
        case ShutdownState::Release: return "RELEASE";
        case ShutdownState::Done: return "DONE";
    }
    return "?";
}

struct ShutdownHooks {
    std::function<bool()> drained;                  // the run loop has returned: nothing is in flight any more
    std::function<uint64_t()> inFlight;             // shares still without an outcome
    std::function<void(uint64_t)> markUnknown;      // record n in-flight shares as unknown (terminal; never replayed)
    std::function<bool(bool forced, uint64_t unknown, const std::string &reason)> persist;
    std::function<bool()> close;
    std::function<bool()> release;
};

class ShutdownMachine {
   public:
    ShutdownMachine(uint64_t drainMs, ShutdownHooks hooks) : drainMs_(drainMs), hooks_(std::move(hooks)) { trace_.push_back(ShutdownState::Running); }

    /** Thread-safe, idempotent: the first request fixes the reason. */
    void requestStop(const std::string &reason) {
        bool expected = false;
        if (requested_.compare_exchange_strong(expected, true)) {
            std::lock_guard<std::mutex> lock(mu_);
            reason_ = reason;
        }
    }

    bool stopRequested() const { return requested_.load(); }
    /** Watcher thread only, before requestStop(): a close event skips the drain (deadline = now). */
    void setDrainMs(uint64_t ms) { drainMs_ = ms; }
    ShutdownState state() const { return state_.load(); }
    bool done() const { return state_.load() == ShutdownState::Done; }
    bool forced() const { return forced_; }
    uint64_t unknownRecorded() const { return unknown_; }
    bool persistFailed() const { return persistFailed_; }
    const std::vector<ShutdownState> &trace() const { return trace_; }

    /** Drive the machine. Returns the state after at most one transition. Call from one thread only. */
    ShutdownState step(uint64_t nowMs) {
        switch (state_.load()) {
            case ShutdownState::Running:
                if (requested_.load()) { deadlineMs_ = nowMs + drainMs_; go(ShutdownState::Draining); }
                break;
            case ShutdownState::Draining:
                if (hooks_.drained && hooks_.drained()) {
                    go(ShutdownState::Persist);
                } else if (nowMs >= deadlineMs_) {
                    forced_ = true;
                    const uint64_t n = hooks_.inFlight ? hooks_.inFlight() : 0;
                    if (n && hooks_.markUnknown) hooks_.markUnknown(n);
                    unknown_ = n;
                    go(ShutdownState::Persist);
                }
                break;
            case ShutdownState::Persist: {
                std::string reason;
                { std::lock_guard<std::mutex> lock(mu_); reason = reason_; }
                if (hooks_.persist && !hooks_.persist(forced_, unknown_, reason)) persistFailed_ = true;  // continue: shutdown must finish
                go(ShutdownState::Close);
                break;
            }
            case ShutdownState::Close:
                if (hooks_.close) hooks_.close();
                go(ShutdownState::Release);
                break;
            case ShutdownState::Release:
                if (hooks_.release) hooks_.release();
                go(ShutdownState::Done);
                break;
            case ShutdownState::Done: break;
        }
        return state_.load();
    }

   private:
    uint64_t drainMs_;
    ShutdownHooks hooks_;
    std::atomic<bool> requested_{false};
    std::atomic<ShutdownState> state_{ShutdownState::Running};
    std::mutex mu_;
    std::string reason_;
    uint64_t deadlineMs_ = 0, unknown_ = 0;
    bool forced_ = false, persistFailed_ = false;
    std::vector<ShutdownState> trace_;

    void go(ShutdownState s) {
        state_.store(s);
        trace_.push_back(s);
    }
};

}  // namespace om
