// Default-off native duration accounting; no clock, device, or transport calls.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace om {
struct PearlSustainedPolicy {
    unsigned seconds = 0, minimumAccepted = 0;
    bool publicRun = false;
    bool public24h = false;
};
inline bool pearlSustainedPolicyValid(const PearlSustainedPolicy &policy) noexcept {
    if (policy.public24h) return policy.publicRun && policy.seconds == 86400 && policy.minimumAccepted == 20;
    return policy.publicRun ? policy.seconds == 7200 && policy.minimumAccepted == 20 :
        policy.seconds >= 1 && policy.seconds <= 60 && policy.minimumAccepted == 1;
}
inline const char *pearlSustainedMode(const PearlSustainedPolicy &policy) noexcept {
    return policy.public24h ? "public-24h-v1" : policy.publicRun ? "public-v1" : "private-v1";
}
inline bool pearlParseSustained(const char *mode, const char *seconds,
                                PearlSustainedPolicy *out) noexcept {
    if (!out) return false;
    *out = {};
    if (!mode && !seconds) return true;
    if (!mode || !seconds) return false;
    if (!std::strcmp(mode, "public-24h-v1")) {
        if (std::strcmp(seconds, "86400")) return false;
        *out = {86400, 20, true, true};
        return true;
    }
    if (!std::strcmp(mode, "public-v1")) {
        if (std::strcmp(seconds, "7200")) return false;
        *out = {7200, 20, true};
        return true;
    }
    if (std::strcmp(mode, "private-v1") || seconds[0] < '1' || seconds[0] > '9') return false;
    unsigned value = static_cast<unsigned>(seconds[0] - '0');
    if (seconds[1]) {
        if (seconds[1] < '0' || seconds[1] > '9' || seconds[2]) return false;
        value = value * 10 + static_cast<unsigned>(seconds[1] - '0');
    }
    if (value > 60) return false;
    *out = {value, 1, false};
    return true;
}
struct PearlSustainedFinal {
    PearlSustainedPolicy policy;
    int64_t activeStartNs = 0, deadlineNs = 0, stopNs = 0, drainNs = 0;
};
inline bool pearlSustainedFinalValid(const PearlSustainedFinal &value, uint64_t accepted) noexcept {
    if (!pearlSustainedPolicyValid(value.policy) || value.activeStartNs < 0) return false;
    const int64_t duration = int64_t(value.policy.seconds) * INT64_C(1000000000);
    return value.activeStartNs <= std::numeric_limits<int64_t>::max() - duration &&
        value.deadlineNs == value.activeStartNs + duration && value.stopNs >= value.deadlineNs &&
        value.drainNs >= value.stopNs && accepted >= value.policy.minimumAccepted;
}
class PearlSustainedTracker final {
public:
    explicit PearlSustainedTracker(PearlSustainedPolicy policy) noexcept : policy_(policy) {}
    bool started() const noexcept { return started_; }
    bool reached() const noexcept { return reached_ && !failed_; }
    int64_t activeStartNs() const noexcept { return start_; }
    int64_t deadlineNs() const noexcept { return deadline_; }
    int64_t stopNs() const noexcept { return stop_; }
    bool start(int64_t now) noexcept {
        if (failed_ || started_ || !pearlSustainedPolicyValid(policy_) || now < 0 ||
            now > std::numeric_limits<int64_t>::max() - int64_t(policy_.seconds) * INT64_C(1000000000))
            return fail();
        started_ = true;
        start_ = last_ = now;
        deadline_ = now + int64_t(policy_.seconds) * INT64_C(1000000000);
        return true;
    }
    bool poll(int64_t now, bool externalStop) noexcept {
        if (failed_ || !started_ || finished_ || externalStop || now < last_) return fail();
        last_ = now;
        if (!reached_ && accounted_ && now >= deadline_) {
            reached_ = true;
            stop_ = now;
        }
        return true;
    }
    // Snapshot/status definitions live in run.h. Duck typing keeps this pure
    // header independent of run.h and usable inside its option declarations.
    template <class Status, class Snapshot>
    bool observe(int64_t now, Status status, const Snapshot &next,
                 std::size_t outputs, bool externalStop) noexcept {
        return observeImpl(now,status,next,outputs,externalStop,false);
    }
#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
    template <class Status,class Snapshot>
    bool observeDeferred(int64_t now,Status status,const Snapshot &next,
                         std::size_t outputs,bool externalStop) noexcept {
        return observeImpl(now,status,next,outputs,externalStop,true);
    }
    // No new search/submission is permitted after the duration boundary. Only
    // the one already-accounted pending share may gain a terminal acceptance.
    template <class Status,class Snapshot>
    bool settleDeferred(int64_t now,Status status,const Snapshot &next,bool externalStop) noexcept {
        if(!reached() || finished_ || externalStop || now<last_ || status!=Status::Valid ||
            !next.conserved() || next.pending || next.rejected || next.stale || next.unknown ||
            next.submitted!=submitted_ || next.accepted<accepted_ ||
            next.accepted-accepted_>pending_)return fail();
        accepted_=next.accepted;pending_=0;last_=now;
        return true;
    }
#endif
private:
    template <class Status,class Snapshot>
    bool observeImpl(int64_t now,Status status,const Snapshot &next,
                     std::size_t outputs,bool externalStop,bool deferred) noexcept {
        if (failed_ || !started_ || reached_ || finished_ || externalStop || now < last_ ||
            status != Status::Valid || !next.conserved() || next.pending>(deferred?1u:0u) || next.rejected ||
            next.stale || next.unknown || outputs > 1 || next.accepted < accepted_ ||
            next.accepted - accepted_ > outputs+(deferred?pending_:0) ||
            next.submitted<submitted_ || next.submitted-submitted_>outputs ||
            accounted_ == std::numeric_limits<uint64_t>::max())
            return fail();
        accepted_ = next.accepted;
        submitted_=next.submitted;pending_=next.pending;
        ++accounted_;
        return poll(now, externalStop);
    }
public:
    bool finish(int64_t now, bool externalStop, PearlSustainedFinal *out) noexcept {
        if (!out || !reached() || finished_ || externalStop || now < last_ || pending_) return fail();
        PearlSustainedFinal value{policy_, start_, deadline_, stop_, now};
        if (!pearlSustainedFinalValid(value, accepted_)) return fail();
        last_ = now;
        finished_ = true;
        *out = value;
        return true;
    }
private:
    bool fail() noexcept { failed_ = true; return false; }
    const PearlSustainedPolicy policy_;
    bool started_ = false, reached_ = false, failed_ = false, finished_ = false;
    uint64_t accepted_ = 0, accounted_ = 0;
    uint64_t submitted_=0,pending_=0;
    int64_t start_ = 0, last_ = 0, deadline_ = 0, stop_ = 0;
};
} // namespace om
