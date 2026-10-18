// Canonical, pure-host identity for one serialized Pearl Solution.
//
// This file allocates no ownership token and performs no coordinator, source,
// network, GPU, or backend work. It only hashes a bounded, versioned byte
// encoding of the exact result that would be submitted.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

#include "../../core/algo.h"
#include "job.h"

namespace om {
namespace pearl {

constexpr std::size_t kSolutionFingerprintBytes = 32;
// The largest legal current Pearl proof is below this defensive identity-layer
// bound. Production integration must reject rather than hash anything larger.
constexpr std::size_t kSolutionFingerprintMaxProofBytes = 16 * 1024 * 1024;

struct SolutionFingerprint {
    std::array<uint8_t, kSolutionFingerprintBytes> bytes{};
};

namespace solution_identity_detail {

// The terminating NUL is part of the domain separator. Changing any byte or
// field order requires a new domain/version.
constexpr char kDomain[] = "canary-pearl-solution-v1";
static_assert(sizeof(kDomain) == 25,
              "the Pearl solution-identity domain includes its final NUL");
constexpr std::size_t kFixedEncodingBytes =
    sizeof(kDomain) + sizeof(uint64_t) + 4 * sizeof(uint64_t) +
    sizeof(uint64_t);

inline void storeU64Be(uint64_t value, uint8_t *out) noexcept {
    for (unsigned i = 0; i < 8; ++i)
        out[i] = static_cast<uint8_t>(value >> (56 - 8 * i));
}

inline bool proofLengthFitsU64(std::size_t proofBytes) noexcept {
    if constexpr (sizeof(std::size_t) > sizeof(uint64_t)) {
        return proofBytes <=
               static_cast<std::size_t>(
                   std::numeric_limits<uint64_t>::max());
    }
    (void)proofBytes;
    return true;
}

}  // namespace solution_identity_detail

namespace solution_identity_detail {

struct HeapBufferFactory {
    uint8_t *operator()(std::size_t bytes) const {
        return new uint8_t[bytes];
    }
};

template <typename BufferFactory>
inline bool computeSolutionFingerprintWithFactory(
    const Solution &solution, SolutionFingerprint *out,
    const BufferFactory &bufferFactory) noexcept {
    if (!out) return false;
    *out = SolutionFingerprint();

    const std::size_t proofBytes = solution.extra.size();
    if (proofBytes > kSolutionFingerprintMaxProofBytes ||
        !proofLengthFitsU64(proofBytes) ||
        proofBytes > std::numeric_limits<std::size_t>::max() -
                         kFixedEncodingBytes)
        return false;

    try {
        const std::size_t encodedBytes = kFixedEncodingBytes + proofBytes;
        std::unique_ptr<uint8_t[]> encoded(bufferFactory(encodedBytes));
        uint8_t *const encodedData = encoded.get();
        if (!encodedData) return false;
        std::size_t offset = 0;
        for (std::size_t i = 0; i < sizeof(kDomain); ++i)
            encodedData[offset++] = static_cast<uint8_t>(kDomain[i]);

        storeU64Be(solution.nonce, encodedData + offset);
        offset += sizeof(uint64_t);
        for (uint64_t limb : solution.hit) {
            storeU64Be(limb, encodedData + offset);
            offset += sizeof(uint64_t);
        }
        storeU64Be(static_cast<uint64_t>(proofBytes),
                   encodedData + offset);
        offset += sizeof(uint64_t);
        for (std::size_t i = 0; i < proofBytes; ++i)
            encodedData[offset + i] =
                static_cast<uint8_t>(solution.extra[i]);

        SolutionFingerprint candidate;
        b3::hash(nullptr, encodedData, encodedBytes,
                 candidate.bytes.data());
        *out = candidate;
        return true;
    } catch (...) {
        *out = SolutionFingerprint();
        return false;
    }
}

}  // namespace solution_identity_detail

/**
 * BLAKE3("canary-pearl-solution-v1\\0" || nonce_u64_be ||
 *        hit[0]_u64_be || ... || hit[3]_u64_be || proof_len_u64_be ||
 *        exact Solution::extra bytes).
 *
 * Returns false and leaves a zero fingerprint for null output, excessive or
 * unrepresentable proof length, size overflow, or any allocation/hash
 * exception. Embedded NUL bytes in `extra` are data, never terminators.
 */
inline bool computeSolutionFingerprint(const Solution &solution,
                                       SolutionFingerprint *out) noexcept {
    return solution_identity_detail::computeSolutionFingerprintWithFactory(
        solution, out, solution_identity_detail::HeapBufferFactory());
}

inline bool solutionFingerprintEqual(const SolutionFingerprint &a,
                                     const SolutionFingerprint &b) noexcept {
    return a.bytes == b.bytes;
}

}  // namespace pearl
}  // namespace om
