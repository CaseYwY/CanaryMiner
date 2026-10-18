#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace om { namespace pearl { namespace proof_timing {

// Diagnostic only. In particular, this never authorizes a proof, consumes a
// ledger token, changes a return value, or introduces a device synchronization.
inline bool enabled(const char *value) noexcept {
    return value && value[0] == '1' && value[1] == '\0';
}

template<std::size_t N> struct Sample {
    std::array<uint64_t, N> stageNs{};
    uint64_t nonce = 0, proofToken = 0, startNs = 0, endNs = 0, totalNs = 0, tailNs = 0;
    uint32_t m = 0, n = 0, k = 0, rank = 0;
    std::size_t stages = 0;
    bool complete = false, valid = true;
};

struct SystemAdapter {
    uint64_t now() const noexcept {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    template<std::size_t N> void publish(const Sample<N> &s) const noexcept {
        static_assert(N == 6 || N == 2, "only native and source-presubmit diagnostics");
        const char *status = !s.valid ? "invalid" : s.complete ? "complete" : "partial";
        // Numeric identifiers only: no wallet, job strings, proof bytes, or
        // protocol output. A zero proof token is an unbound/failed finalizer.
        // Each line is emitted in one stdio call to avoid interleaved fragments.
        if constexpr (N == 6) {
            std::fprintf(stderr,
                "[pearl-proof-timing] v=1 scope=native nonce=%016llx proof_token=%llu "
                "m=%u n=%u k=%u rank=%u a_bytes=%llu bt_bytes=%llu "
                "status=%s stages=%zu readback_ns=%llu host_copy_ns=%llu recheck_ns=%llu merkle_a_ns=%llu "
                "merkle_b_ns=%llu check_encode_ns=%llu tail_ns=%llu total_ns=%llu start_ns=%llu end_ns=%llu\n",
                static_cast<unsigned long long>(s.nonce), static_cast<unsigned long long>(s.proofToken),
                s.m, s.n, s.k, s.rank,
                static_cast<unsigned long long>(uint64_t{s.m} * s.k),
                static_cast<unsigned long long>(uint64_t{s.n} * s.k),
                status, s.stages, static_cast<unsigned long long>(s.stageNs[0]),
                static_cast<unsigned long long>(s.stageNs[1]), static_cast<unsigned long long>(s.stageNs[2]),
                static_cast<unsigned long long>(s.stageNs[3]), static_cast<unsigned long long>(s.stageNs[4]),
                static_cast<unsigned long long>(s.stageNs[5]),
                static_cast<unsigned long long>(s.tailNs), static_cast<unsigned long long>(s.totalNs),
                static_cast<unsigned long long>(s.startNs), static_cast<unsigned long long>(s.endNs));
        } else {
            std::fprintf(stderr,
                "[pearl-proof-timing] v=1 scope=source-presubmit nonce=%016llx proof_token=%llu "
                "status=%s stages=%zu preparation_ns=%llu intake_ns=%llu tail_ns=%llu total_ns=%llu start_ns=%llu end_ns=%llu\n",
                static_cast<unsigned long long>(s.nonce), static_cast<unsigned long long>(s.proofToken),
                status, s.stages, static_cast<unsigned long long>(s.stageNs[0]),
                static_cast<unsigned long long>(s.stageNs[1]), static_cast<unsigned long long>(s.tailNs),
                static_cast<unsigned long long>(s.totalNs),
                static_cast<unsigned long long>(s.startNs), static_cast<unsigned long long>(s.endNs));
        }
    }
};

struct NetworkAdapter : SystemAdapter {
    void publish(const Sample<1> &s) const noexcept {
        const char *status = !s.valid ? "invalid" : s.complete ? "complete" : "partial";
        std::fprintf(stderr,
            "[pearl-proof-timing] v=1 scope=source-network nonce=%016llx proof_token=%llu "
            "status=%s stages=%zu send_ns=%llu reply_and_finalize_ns=%llu total_ns=%llu start_ns=%llu end_ns=%llu\n",
            static_cast<unsigned long long>(s.nonce), static_cast<unsigned long long>(s.proofToken),
            status, s.stages, static_cast<unsigned long long>(s.stageNs[0]),
            static_cast<unsigned long long>(s.tailNs), static_cast<unsigned long long>(s.totalNs),
            static_cast<unsigned long long>(s.startNs), static_cast<unsigned long long>(s.endNs));
    }
};

// Complete means verification returned, not that the proof was valid.
struct HostVerifyAdapter : SystemAdapter {
    void publish(const Sample<1> &s) const noexcept {
        const char *status = !s.valid ? "invalid" : s.complete ? "complete" : "partial";
        std::fprintf(stderr,
            "[pearl-proof-timing] v=1 scope=host-verify nonce=%016llx proof_token=%llu "
            "status=%s stages=%zu verify_ns=%llu tail_ns=%llu total_ns=%llu start_ns=%llu end_ns=%llu\n",
            static_cast<unsigned long long>(s.nonce), static_cast<unsigned long long>(s.proofToken),
            status, s.stages, static_cast<unsigned long long>(s.stageNs[0]),
            static_cast<unsigned long long>(s.tailNs), static_cast<unsigned long long>(s.totalNs),
            static_cast<unsigned long long>(s.startNs), static_cast<unsigned long long>(s.endNs));
    }
};

// The injectable adapter lets CPU tests execute the production state machine.
// Disabled scopes do not read the clock or emit output. Partial/invalid samples
// are labelled rather than made to resemble successful full-path timings.
template<std::size_t N, class Adapter = SystemAdapter> class Scope {
public:
    Scope(const char *option, uint64_t nonce, uint64_t token = 0, Adapter adapter = {}) noexcept
        : adapter_(adapter), enabled_(enabled(option)) {
        sample_.nonce = nonce;
        sample_.proofToken = token;
        if (enabled_) sample_.startNs = start_ = previous_ = adapter_.now();
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
    ~Scope() noexcept {
        if (!enabled_) return;
        if (!finished_) close(completeOnExit_);
        adapter_.publish(sample_);
    }
    void proofToken(uint64_t token) noexcept {
        if (enabled_ && !finished_) sample_.proofToken = token;
    }
    void shape(uint32_t m, uint32_t n, uint32_t k, uint32_t rank) noexcept {
        if (!enabled_ || finished_) return;
        sample_.m = m; sample_.n = n; sample_.k = k; sample_.rank = rank;
    }
    void boundary() noexcept {
        if (!enabled_) return;
        if (finished_ || sample_.stages == N) { sample_.valid = false; return; }
        const uint64_t t = adapter_.now();
        if (t < previous_) sample_.valid = false;
        else sample_.stageNs[sample_.stages] = t - previous_;
        previous_ = t;
        ++sample_.stages;
    }
    // Freeze before network send/reply waiting. Destruction only prints the
    // frozen record, so source-presubmit never silently includes pool RTT.
    void finish() noexcept { if (enabled_ && !finished_) close(true); }
    // Native scope is declared before the host buffers. Its destructor therefore
    // includes their cleanup in total/tail, rather than losing it at return.
    void completeOnExit() noexcept { if (enabled_ && !finished_) completeOnExit_ = true; }
private:
    void close(bool complete) noexcept {
        const uint64_t t = adapter_.now();
        sample_.endNs = t;
        if (t < previous_ || t < start_) sample_.valid = false;
        else { sample_.totalNs = t - start_; sample_.tailNs = t - previous_; }
        sample_.complete = complete && sample_.stages == N && sample_.valid;
        if (complete && sample_.stages != N) sample_.valid = false;
        finished_ = true;
    }
    Adapter adapter_;
    Sample<N> sample_;
    uint64_t start_ = 0, previous_ = 0;
    bool enabled_ = false, finished_ = false, completeOnExit_ = false;
};

using HostVerifyScope = Scope<1, HostVerifyAdapter>;
using NativeScope = Scope<6>;
using SourceScope = Scope<2>;
using NetworkScope = Scope<1, NetworkAdapter>;

}}} // namespace om::pearl::proof_timing
