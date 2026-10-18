// Pearl search control and hit-buffer ownership policy.
//
// This header deliberately contains no CUDA types.  The production CUDA path
// uses it, while host-only tests can exercise the control-flow invariants that
// previously existed only as comments around device memory.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace om::pearl {

// Must match the allocation and powScan capacity in algo.cu.  Keeping one
// named value prevents a test from proving a different capacity than ships.
constexpr uint32_t kMaxHits = 64;

/** Enumerate every nonce attempt in one search call, exactly once. */
class AttemptSchedule {
   public:
    AttemptSchedule(uint64_t nonceBase, uint64_t attempts)
        : base_(nonceBase), attempts_(attempts) {}

    bool next(uint64_t *nonce) {
        if (!nonce || offset_ >= attempts_ ||
            offset_ > std::numeric_limits<uint64_t>::max() - base_)
            return false;
        *nonce = base_ + offset_++;
        return true;
    }

    uint64_t completed() const { return offset_; }
    bool complete() const { return offset_ == attempts_; }

   private:
    uint64_t base_ = 0;
    uint64_t attempts_ = 0;
    uint64_t offset_ = 0;
};

/** What the atomic hit counter says versus what fits in its output buffers. */
struct HitWindow {
    uint32_t observed = 0;
    uint32_t retained = 0;
    bool saturated = false;
};

inline HitWindow hitWindow(uint32_t observed) {
    return {observed, std::min(observed, kMaxHits), observed > kMaxHits};
}

/**
 * Select the lowest tile from only the prefix owned by this attempt.
 *
 * Bytes beyond `window.retained` may contain hits from a prior attempt.  They
 * are intentionally unobservable here.  `available` is the number of entries
 * actually copied from the device and protects callers from a short read.
 */
inline bool selectLowestRetainedHit(const uint32_t *indices, size_t available,
                                    const HitWindow &window,
                                    uint32_t *selectedSlot) {
    if (!indices || !selectedSlot || window.retained == 0 ||
        window.retained > available || window.retained > kMaxHits ||
        window.retained > window.observed)
        return false;
    uint32_t slot = 0;
    for (uint32_t i = 1; i < window.retained; ++i)
        if (indices[i] < indices[slot]) slot = i;
    *selectedSlot = slot;
    return true;
}

/**
 * Records that may authorize proof submission for the current search call.
 *
 * beginSearch() invalidates every prior record.  Lookup is newest-first so a
 * repeated nonce can never be authorized by stale proof state.
 */
template <typename Record>
class ProofOwnershipLedger {
   public:
    void beginSearch() {
        records_.clear();
        ++generation_;
    }

    void record(const Record &record) { records_.push_back(record); }

    template <typename NonceOf>
    const Record *newest(uint64_t nonce, NonceOf nonceOf) const {
        for (auto it = records_.rbegin(); it != records_.rend(); ++it)
            if (nonceOf(*it) == nonce) return &*it;
        return nullptr;
    }

    size_t size() const { return records_.size(); }
    uint64_t generation() const { return generation_; }

   private:
    std::vector<Record> records_;
    uint64_t generation_ = 0;
};

}  // namespace om::pearl
