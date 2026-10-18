#pragma once
// H-019 task 2 (live v28u): a clean non-transport guard stop may restart the GPU
// in a fresh episode, at most kRestarts times in a rolling window, after kDelayMs.
// The fifth stop inside the window ends the session. Transport retries are a
// separate path (reconnect1, uncapped) and never call admit().
#include <stddef.h>
#include <stdint.h>

#include <vector>

namespace om {

class GuardRestartBudget {
   public:
    static constexpr unsigned kRestarts = 3;
    static constexpr uint64_t kWindowMs = 3600 * 1000;
    static constexpr uint64_t kDelayMs = 10 * 1000;

    /** One clean guard stop at `nowMs` (monotonic). True: restart after kDelayMs.
     * False: the budget is spent for the rolling window, so the session stops. */
    bool admit(uint64_t nowMs) {
        prune(nowMs);
        if (restarts_.size() >= kRestarts) return false;
        restarts_.push_back(nowMs);
        return true;
    }

    size_t usedInWindow(uint64_t nowMs) {
        prune(nowMs);
        return restarts_.size();
    }

   private:
    void prune(uint64_t nowMs) {
        size_t keep = 0;
        for (uint64_t t : restarts_)
            if (nowMs - t < kWindowMs) restarts_[keep++] = t;
        restarts_.resize(keep);
    }

    std::vector<uint64_t> restarts_;
};

}  // namespace om
