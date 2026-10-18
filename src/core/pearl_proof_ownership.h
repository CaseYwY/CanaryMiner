// Exact, preallocated ownership for serialized Pearl proofs.
//
// This is an additive pure-host seam. It performs no GPU, source, network, or
// wallet work. A backend may bind an already serialized proof here, and the
// run loop may later consume that binding only by presenting the exact source
// job, search generation, and solution metadata.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

#include "../algos/pearl-pow/solution_identity.h"
#include "pearl_source_observability.h"

namespace om {

// A backend emits at most one serialized result per nonce attempt. The run
// planner must split larger batches before opening this ledger. This defensive
// ceiling is intentionally unrelated to the 64-hit *per-attempt* device queue.
constexpr uint64_t kPearlProofOwnershipMaxPerSearch = 4096;

struct PearlProofJobIdentity {
    uint64_t session = 0;
    uint64_t authorization = 0;
    uint64_t job = 0;
    PearlJobIdentityDigest digest;
};

struct PearlSerializedProofMetadata {
    uint64_t nonce = 0;
    std::array<uint64_t, 4> hit{};
    pearl::SolutionFingerprint fingerprint;
    uint64_t proofBytes = 0;
};

struct PearlProofBinding {
    uint64_t token = 0;
    PearlProofJobIdentity job;
    uint64_t searchGeneration = 0;
    PearlSerializedProofMetadata proof;
};

inline bool pearlProofMemoryRangesOverlap(
        const void *a, std::size_t aBytes,
        const void *b, std::size_t bBytes) noexcept {
    if (!a || !b || aBytes == 0 || bBytes == 0) return false;
    const std::uintptr_t aBegin = reinterpret_cast<std::uintptr_t>(a);
    const std::uintptr_t bBegin = reinterpret_cast<std::uintptr_t>(b);
    if (aBytes > std::numeric_limits<std::uintptr_t>::max() - aBegin ||
        bBytes > std::numeric_limits<std::uintptr_t>::max() - bBegin)
        return true;
    const std::uintptr_t aEnd = aBegin + aBytes;
    const std::uintptr_t bEnd = bBegin + bBytes;
    return aBegin < bEnd && bBegin < aEnd;
}

inline bool pearlProofDigestZero(
        const PearlJobIdentityDigest &digest) noexcept {
    for (uint8_t byte : digest.bytes)
        if (byte != 0) return false;
    return true;
}

inline bool pearlProofFingerprintZero(
        const pearl::SolutionFingerprint &fingerprint) noexcept {
    for (uint8_t byte : fingerprint.bytes)
        if (byte != 0) return false;
    return true;
}

inline bool pearlProofJobIdentityValid(
        const PearlProofJobIdentity &identity) noexcept {
    return identity.session != 0 && identity.authorization != 0 &&
           identity.job != 0 && !pearlProofDigestZero(identity.digest);
}

inline bool pearlSerializedProofMetadataValid(
        const PearlSerializedProofMetadata &metadata) noexcept {
    return metadata.proofBytes != 0 &&
           metadata.proofBytes <= pearl::kSolutionFingerprintMaxProofBytes &&
           !pearlProofFingerprintZero(metadata.fingerprint);
}

inline bool pearlProofJobIdentityEqual(
        const PearlProofJobIdentity &a,
        const PearlProofJobIdentity &b) noexcept {
    return a.session == b.session &&
           a.authorization == b.authorization && a.job == b.job &&
           a.digest.bytes == b.digest.bytes;
}

inline bool pearlSerializedProofMetadataEqual(
        const PearlSerializedProofMetadata &a,
        const PearlSerializedProofMetadata &b) noexcept {
    return a.nonce == b.nonce && a.hit == b.hit &&
           a.fingerprint.bytes == b.fingerprint.bytes &&
           a.proofBytes == b.proofBytes;
}

/** Build exact metadata from the submitted representation of a Solution. */
inline bool pearlSerializedProofMetadataFromSolution(
        const Solution &solution,
        PearlSerializedProofMetadata *out) noexcept {
    if (!out) return false;
    if (pearlProofMemoryRangesOverlap(out, sizeof(*out), &solution,
                                      sizeof(solution)))
        return false;
    *out = PearlSerializedProofMetadata();
    if (solution.extra.empty() ||
        solution.extra.size() > pearl::kSolutionFingerprintMaxProofBytes ||
        !pearl::solution_identity_detail::proofLengthFitsU64(
            solution.extra.size()))
        return false;

    PearlSerializedProofMetadata candidate;
    candidate.nonce = solution.nonce;
    for (std::size_t i = 0; i < candidate.hit.size(); ++i)
        candidate.hit[i] = solution.hit[i];
    candidate.proofBytes = static_cast<uint64_t>(solution.extra.size());
    if (!pearl::computeSolutionFingerprint(solution,
                                            &candidate.fingerprint) ||
        !pearlSerializedProofMetadataValid(candidate))
        return false;
    *out = candidate;
    return true;
}

/**
 * Single-open-search, fail-closed proof ledger.
 *
 * beginSearch() reserves the caller-declared maximum number of serialized
 * results before device work starts. bind() cannot allocate afterward. Any
 * integrity mismatch permanently quarantines the ledger. Declared-capacity,
 * allocation, and token exhaustion also quarantine it because continuing
 * would make proof ownership ambiguous. Normal search failure may explicitly
 * invalidate the one open search without clearing an existing quarantine.
 */
class PearlProofOwnershipLedger {
   public:
    PearlProofOwnershipLedger() = default;
    PearlProofOwnershipLedger(const PearlProofOwnershipLedger &) = delete;
    PearlProofOwnershipLedger &operator=(
        const PearlProofOwnershipLedger &) = delete;
    PearlProofOwnershipLedger(PearlProofOwnershipLedger &&) = delete;
    PearlProofOwnershipLedger &operator=(
        PearlProofOwnershipLedger &&) = delete;

    bool beginSearch(const PearlProofJobIdentity &job,
                     uint64_t searchGeneration,
                     uint64_t maximumProofs) noexcept {
        if (quarantined_ || searchOpen_ || !records_.empty() ||
            inputOverlapsLedger(&job, sizeof(job)) ||
            !pearlProofJobIdentityValid(job) || searchGeneration == 0 ||
            searchGeneration <= lastSearchGeneration_ ||
            maximumProofs == 0 ||
            maximumProofs > kPearlProofOwnershipMaxPerSearch) {
            quarantine();
            return false;
        }
        try {
#if defined(PEARL_PROOF_OWNERSHIP_ENABLE_TEST_SEAMS)
            if (failNextReserveForTest_) {
                failNextReserveForTest_ = false;
                throw std::bad_alloc();
            }
#endif
            records_.reserve(static_cast<std::size_t>(maximumProofs));
        } catch (...) {
            quarantine();
            return false;
        }
        activeJob_ = job;
        activeSearchGeneration_ = searchGeneration;
        lastSearchGeneration_ = searchGeneration;
        maximumProofs_ = static_cast<std::size_t>(maximumProofs);
        searchOpen_ = true;
        return true;
    }

    bool bind(const PearlProofJobIdentity &job, uint64_t searchGeneration,
              const PearlSerializedProofMetadata &proof,
              uint64_t *token) noexcept {
        if (token &&
            (outputOverlapsLedger(token, sizeof(*token)) ||
             pearlProofMemoryRangesOverlap(token, sizeof(*token), &job,
                                           sizeof(job)) ||
             pearlProofMemoryRangesOverlap(token, sizeof(*token), &proof,
                                           sizeof(proof)))) {
            quarantine();
            return false;
        }
        if (token) *token = 0;
        if (!token || quarantined_ || !searchOpen_ ||
            inputOverlapsLedger(&job, sizeof(job)) ||
            inputOverlapsLedger(&proof, sizeof(proof)) ||
            !pearlProofJobIdentityValid(job) || searchGeneration == 0 ||
            !pearlProofJobIdentityEqual(job, activeJob_) ||
            searchGeneration != activeSearchGeneration_ ||
            !pearlSerializedProofMetadataValid(proof)) {
            quarantine();
            return false;
        }
        if (records_.size() >= maximumProofs_ ||
            lastToken_ == std::numeric_limits<uint64_t>::max()) {
            quarantine();
            return false;
        }
        for (const PearlProofBinding &record : records_) {
            if (pearlSerializedProofMetadataEqual(record.proof, proof)) {
                quarantine();
                return false;
            }
        }

        PearlProofBinding candidate;
        candidate.token = lastToken_ + 1;
        candidate.job = job;
        candidate.searchGeneration = searchGeneration;
        candidate.proof = proof;
        try {
            records_.push_back(candidate);
        } catch (...) {
            quarantine();
            return false;
        }
        lastToken_ = candidate.token;
        *token = candidate.token;
        return true;
    }

    bool consume(uint64_t token, const PearlProofJobIdentity &job,
                 uint64_t searchGeneration,
                 const PearlSerializedProofMetadata &proof,
        PearlProofBinding *out) noexcept {
        if (out &&
            (outputOverlapsLedger(out, sizeof(*out)) ||
             pearlProofMemoryRangesOverlap(out, sizeof(*out), &job,
                                           sizeof(job)) ||
             pearlProofMemoryRangesOverlap(out, sizeof(*out), &proof,
                                           sizeof(proof)))) {
            quarantine();
            return false;
        }
        if (out) *out = PearlProofBinding();
        if (!out || quarantined_ || !searchOpen_ || token == 0 ||
            inputOverlapsLedger(&job, sizeof(job)) ||
            inputOverlapsLedger(&proof, sizeof(proof)) ||
            !pearlProofJobIdentityValid(job) || searchGeneration == 0 ||
            !pearlProofJobIdentityEqual(job, activeJob_) ||
            searchGeneration != activeSearchGeneration_ ||
            !pearlSerializedProofMetadataValid(proof)) {
            quarantine();
            return false;
        }
        std::size_t index = records_.size();
        for (std::size_t i = 0; i < records_.size(); ++i) {
            if (records_[i].token == token) {
                index = i;
                break;
            }
        }
        if (index == records_.size()) {
            quarantine();
            return false;
        }
        const PearlProofBinding candidate = records_[index];
        if (!pearlProofJobIdentityEqual(candidate.job, job) ||
            candidate.searchGeneration != searchGeneration ||
            !pearlSerializedProofMetadataEqual(candidate.proof, proof)) {
            quarantine();
            return false;
        }

        records_.erase(records_.begin() + static_cast<std::ptrdiff_t>(index));
        *out = candidate;
        return true;
    }

    std::size_t invalidateSearch(const PearlProofJobIdentity &job,
                                 uint64_t searchGeneration) noexcept {
        if (quarantined_ || !searchOpen_ ||
            inputOverlapsLedger(&job, sizeof(job)) ||
            !pearlProofJobIdentityValid(job) || searchGeneration == 0 ||
            !pearlProofJobIdentityEqual(job, activeJob_) ||
            searchGeneration != activeSearchGeneration_) {
            quarantine();
            return 0;
        }
        const std::size_t removed = records_.size();
        closeSearch();
        return removed;
    }

    bool reconcileSearch(const PearlProofJobIdentity &job,
                         uint64_t searchGeneration) noexcept {
        if (quarantined_ || !searchOpen_ ||
            inputOverlapsLedger(&job, sizeof(job)) ||
            !pearlProofJobIdentityValid(job) || searchGeneration == 0 ||
            !pearlProofJobIdentityEqual(job, activeJob_) ||
            searchGeneration != activeSearchGeneration_) {
            quarantine();
            return false;
        }
        if (!records_.empty()) {
            quarantine();
            return false;
        }
        closeSearch();
        return true;
    }

    bool quarantined() const noexcept { return quarantined_; }
    bool searchOpen() const noexcept { return searchOpen_; }
    std::size_t activeCount() const noexcept { return records_.size(); }
    std::size_t maximumProofs() const noexcept { return maximumProofs_; }
    uint64_t lastToken() const noexcept { return lastToken_; }

#if defined(PEARL_PROOF_OWNERSHIP_ENABLE_TEST_SEAMS)
    void setLastTokenForTest(uint64_t token) noexcept { lastToken_ = token; }
    void failNextReserveForTest() noexcept { failNextReserveForTest_ = true; }
    PearlProofBinding *bindingStorageForTest() noexcept {
        return records_.data();
    }
    const PearlProofJobIdentity *activeJobForTest() const noexcept {
        return &activeJob_;
    }
#endif

   private:
    void quarantine() noexcept { quarantined_ = true; }

    bool outputOverlapsLedger(const void *output,
                              std::size_t bytes) const noexcept {
        if (pearlProofMemoryRangesOverlap(output, bytes, this, sizeof(*this)))
            return true;
        return records_.data() && records_.capacity() != 0 &&
               pearlProofMemoryRangesOverlap(
                   output, bytes, records_.data(),
                   records_.capacity() * sizeof(PearlProofBinding));
    }

    bool inputOverlapsLedger(const void *input,
                             std::size_t bytes) const noexcept {
        return outputOverlapsLedger(input, bytes);
    }

    void closeSearch() noexcept {
        records_.clear();
        activeJob_ = PearlProofJobIdentity();
        activeSearchGeneration_ = 0;
        maximumProofs_ = 0;
        searchOpen_ = false;
    }

    std::vector<PearlProofBinding> records_;
    PearlProofJobIdentity activeJob_;
    uint64_t activeSearchGeneration_ = 0;
    std::size_t maximumProofs_ = 0;
    uint64_t lastToken_ = 0;
    uint64_t lastSearchGeneration_ = 0;
    bool searchOpen_ = false;
    bool quarantined_ = false;
#if defined(PEARL_PROOF_OWNERSHIP_ENABLE_TEST_SEAMS)
    bool failNextReserveForTest_ = false;
#endif
};

}  // namespace om
