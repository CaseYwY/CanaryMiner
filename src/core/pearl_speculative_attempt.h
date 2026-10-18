// One worker-owned speculative GPU attempt. This is cache ownership, never
// proof authorization: only promotion by the normal search counts live work.
#pragma once

#include "algo.h"
#include <cstring>
#include <limits>

namespace om {

enum class PearlSpeculativeAttemptState { Empty, Reserved, Running, Ready };

struct PearlSpeculativeAttemptCounters {
    uint64_t reserved = 0;
    uint64_t cancelledBeforeLaunch = 0;
    uint64_t launched = 0;
    uint64_t physicallyCompleted = 0;
    uint64_t promoted = 0;
    uint64_t discarded = 0;
    uint64_t discardedCompleted = 0;
    uint64_t discardedCompletionUnknown = 0;
    uint64_t observedHits = 0;
    uint64_t promotedObservedHits = 0;
    uint64_t discardedObservedHits = 0;
};

class PearlSpeculativeAttemptSlot final {
 public:
    using State = PearlSpeculativeAttemptState;
    PearlSpeculativeAttemptSlot() = default;
    PearlSpeculativeAttemptSlot(const PearlSpeculativeAttemptSlot &) = delete;
    PearlSpeculativeAttemptSlot &operator=(const PearlSpeculativeAttemptSlot &) = delete;

    bool healthy() const noexcept { return !failed_; }
    bool pending() const noexcept { return state_ != State::Empty; }
    State state() const noexcept { return state_; }
    uint64_t nonce() const noexcept { return nonce_; }
    uint64_t generation() const noexcept { return generation_; }
    uint64_t planFingerprint() const noexcept { return plan_; }
    uint64_t observedHits() const noexcept { return hits_; }
    const PearlSpeculativeAttemptCounters &counters() const noexcept { return counters_; }

    // Copy identity before enqueueing anything. No GPU pointer is owned here:
    // the backend must retain its buffers until promote or successful drain.
    bool reserve(const Job &job, uint64_t nonce, uint64_t generation,
                 uint64_t planFingerprint) noexcept {
        if (!healthy() || pending() || !job.valid || !generation || !planFingerprint ||
            !canAdd(counters_.reserved, 1)) return fail();
        try { job_ = job; }
        catch (...) { return fail(); }
        nonce_ = nonce;
        generation_ = generation;
        plan_ = planFingerprint;
        hits_ = 0;
        state_ = State::Reserved;
        ++counters_.reserved;
        return true;
    }

    // Call after the attempt launch was successfully enqueued. Reserved may
    // still contain partial preparation; cancelling it also requires a drain.
    bool markLaunched() noexcept {
        if (!healthy() || state_ != State::Reserved || !canAdd(counters_.launched, 1))
            return fail();
        ++counters_.launched;
        state_ = State::Running;
        return true;
    }

    // Call only after CUDA completion and a bounded hit-count readback. This
    // counts physical work, not an authorized attempt or a serialized proof.
    bool markCompleted(uint64_t observedHits) noexcept {
        if (!healthy()) return false;
        return recordCompletion(observedHits);
    }

    bool matches(const Job &job, uint64_t nonce, uint64_t generation,
                 uint64_t planFingerprint) const noexcept {
        return pending() && nonce == nonce_ && generation == generation_ &&
            planFingerprint == plan_ && job.valid == job_.valid &&
            job.epoch == job_.epoch && job.extra == job_.extra && job.jobId == job_.jobId &&
            !std::memcmp(job.msg, job_.msg, sizeof(job.msg)) &&
            !std::memcmp(job.target, job_.target, sizeof(job.target));
    }

    // A mismatch is an ordinary cache miss and does not consume the slot. The
    // caller must drain/discard it before reserving another identity. The
    // session's normal search/proof ledger remains the only live authority.
    bool promote(const Job &job, uint64_t nonce, uint64_t generation,
                 uint64_t planFingerprint) noexcept {
        if (!healthy() || state_ != State::Ready) return fail();
        if (!matches(job, nonce, generation, planFingerprint)) return false;
        if (!canAdd(counters_.promoted, 1) || !canAdd(counters_.promotedObservedHits, hits_))
            return fail();
        ++counters_.promoted;
        counters_.promotedObservedHits += hits_;
        clear();
        return true;
    }

    // Explicit acknowledgement by the backend that no queued CUDA work can
    // still touch these buffers. Permitted after a sticky error for cleanup.
    // completionKnown=false never labels a running/discarded attempt as a
    // completed zero-hit attempt. A previously Ready slot keeps its hit count.
    bool discardDrained(bool completionKnown = false, uint64_t observedHits = 0) noexcept {
        if (!pending()) return !completionKnown && !observedHits;
        if (state_ == State::Reserved) {
            if (completionKnown || observedHits || !canAdd(counters_.cancelledBeforeLaunch, 1))
                return fail();
            ++counters_.cancelledBeforeLaunch;
            clear();
            return true;
        }
        if (state_ == State::Running) {
            if (!completionKnown && observedHits) return fail();
            if (completionKnown && !recordCompletion(observedHits)) return false;
        } else if (completionKnown && observedHits != hits_) {
            return fail();
        }
        if (!canAdd(counters_.discarded, 1) ||
            (state_ == State::Ready && (!canAdd(counters_.discardedCompleted, 1) ||
                                      !canAdd(counters_.discardedObservedHits, hits_))) ||
            (state_ == State::Running && !canAdd(counters_.discardedCompletionUnknown, 1)))
            return fail();
        ++counters_.discarded;
        if (state_ == State::Ready) {
            ++counters_.discardedCompleted;
            counters_.discardedObservedHits += hits_;
        } else {
            ++counters_.discardedCompletionUnknown;
        }
        clear();
        return true;
    }

    bool conserved() const noexcept {
        const auto &c = counters_;
        return sum(c.reserved, c.cancelledBeforeLaunch, c.launched,
                   state_ == State::Reserved ? 1 : 0) &&
            sum(c.launched, c.promoted, c.discarded,
                state_ == State::Running || state_ == State::Ready ? 1 : 0) &&
            sum(c.physicallyCompleted, c.promoted, c.discardedCompleted,
                state_ == State::Ready ? 1 : 0) &&
            sum(c.discarded, c.discardedCompleted, c.discardedCompletionUnknown, 0) &&
            sum(c.observedHits, c.promotedObservedHits, c.discardedObservedHits,
                state_ == State::Ready ? hits_ : 0);
    }

 private:
    static bool canAdd(uint64_t value, uint64_t amount) noexcept {
        return amount <= std::numeric_limits<uint64_t>::max() - value;
    }
    static bool sum(uint64_t total, uint64_t a, uint64_t b, uint64_t c) noexcept {
        return total >= a && total - a >= b && total - a - b == c;
    }
    bool fail() noexcept { failed_ = true; return false; }
    bool recordCompletion(uint64_t observedHits) noexcept {
        if (state_ != State::Running || !canAdd(counters_.physicallyCompleted, 1) ||
            !canAdd(counters_.observedHits, observedHits)) return fail();
        ++counters_.physicallyCompleted;
        counters_.observedHits += observedHits;
        hits_ = observedHits;
        state_ = State::Ready;
        return true;
    }
    void clear() noexcept {
        job_ = Job{};
        nonce_ = generation_ = plan_ = hits_ = 0;
        state_ = State::Empty;
    }

    Job job_;
    uint64_t nonce_ = 0, generation_ = 0, plan_ = 0, hits_ = 0;
    State state_ = State::Empty;
    PearlSpeculativeAttemptCounters counters_;
    bool failed_ = false;
};

}  // namespace om
