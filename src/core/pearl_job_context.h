// Typed Pearl fork context and target semantics for host-side job validation.
//
// This header deliberately leaves the generic Job ABI unchanged. Pearl source
// adapters encode policy into Job::extra; algorithms and submitters can decode
// and validate it independently before trusting a job.

#pragma once

#include <stdint.h>
#include <string.h>

#include <array>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

#include "algo.h"
#include "../algos/pearl-pow/job.h"

namespace om {

enum class PearlNetwork : uint8_t {
    Mainnet = 1,
    Testnet = 2,
    Testnet2 = 3,
    Regtest = 4,
    SyntheticBenchmark = 5,
};

enum class PearlTargetSemantics : uint8_t {
    BaseBeforePenalty = 1,
    FinalAdjustedBound = 2,
};

enum class PearlCertificateVersion : uint8_t {
    V1 = 1,  // local synthetic benchmark only
    V2 = 2,
    V3 = 3,
};

enum class PearlJobContextError : uint8_t {
    None = 0,
    NullOutput,
    InvalidNetwork,
    InvalidTargetSemantics,
    InvalidCertificateVersion,
    CertificateNotAllowed,
    InvalidSyntheticContext,
    TransportJobIdTooLong,
    EnvelopeTooLong,
    MalformedEnvelope,
    NonCanonicalEnvelope,
    InvalidHeader,
    InvalidJob,
    HeaderMessageMismatch,
    ContextMismatch,
    PolicyMismatch,
    ZeroTarget,
    InvalidMiningConfig,
    TargetOverflow,
    AliasedOutput,
    AllocationFailure,
};

struct PearlSourcePolicy {
    PearlNetwork network = static_cast<PearlNetwork>(0);
    PearlTargetSemantics targetSemantics =
        static_cast<PearlTargetSemantics>(0);
};

struct PearlJobContext {
    PearlNetwork network = static_cast<PearlNetwork>(0);
    PearlTargetSemantics targetSemantics =
        static_cast<PearlTargetSemantics>(0);
    uint64_t height = 0;
    PearlCertificateVersion certificateVersion =
        static_cast<PearlCertificateVersion>(0);
    std::array<uint8_t, 76> header{};
    std::string transportJobId;
};

inline constexpr uint64_t kPearlMainnetV2Activation = 71935;
inline constexpr uint64_t kPearlTestnetV2Activation = 1;
inline constexpr uint64_t kPearlTestnet2V2Activation = 54869;
inline constexpr uint64_t kPearlMainnetRankPenaltyActivation = 96251;
inline constexpr uint64_t kPearlTestnetRankPenaltyActivation = 36761;
inline constexpr uint64_t kPearlTestnet2RankPenaltyActivation = 80627;
inline constexpr uint64_t kPearlRegtestRankPenaltyActivation = 1;
inline constexpr uint64_t kPearlMainnetV3Activation = 99000;
inline constexpr uint64_t kPearlTestnetV3Activation = 38648;
inline constexpr uint64_t kPearlTestnet2V3Activation = 83109;
inline constexpr uint64_t kPearlRegtestV3Activation = 1;
inline constexpr size_t kMaxPearlTransportJobIdBytes = 4096;
inline constexpr size_t kMaxPearlJobEnvelopeBytes = 4352;

inline constexpr uint64_t pearlV2Activation(PearlNetwork network) {
    switch (network) {
    case PearlNetwork::Mainnet: return kPearlMainnetV2Activation;
    case PearlNetwork::Testnet: return kPearlTestnetV2Activation;
    case PearlNetwork::Testnet2: return kPearlTestnet2V2Activation;
    default: return std::numeric_limits<uint64_t>::max();
    }
}

inline constexpr uint64_t pearlRankPenaltyActivation(PearlNetwork network) {
    switch (network) {
    case PearlNetwork::Mainnet: return kPearlMainnetRankPenaltyActivation;
    case PearlNetwork::Testnet: return kPearlTestnetRankPenaltyActivation;
    case PearlNetwork::Testnet2: return kPearlTestnet2RankPenaltyActivation;
    case PearlNetwork::Regtest: return kPearlRegtestRankPenaltyActivation;
    default: return std::numeric_limits<uint64_t>::max();
    }
}

inline constexpr uint64_t pearlV3Activation(PearlNetwork network) {
    switch (network) {
    case PearlNetwork::Mainnet: return kPearlMainnetV3Activation;
    case PearlNetwork::Testnet: return kPearlTestnetV3Activation;
    case PearlNetwork::Testnet2: return kPearlTestnet2V3Activation;
    case PearlNetwork::Regtest: return kPearlRegtestV3Activation;
    default: return std::numeric_limits<uint64_t>::max();
    }
}

inline constexpr bool pearlPublicNetwork(PearlNetwork network) {
    return network == PearlNetwork::Mainnet ||
           network == PearlNetwork::Testnet ||
           network == PearlNetwork::Testnet2;
}

inline constexpr bool pearlKnownNetwork(PearlNetwork network) {
    return pearlPublicNetwork(network) || network == PearlNetwork::Regtest ||
           network == PearlNetwork::SyntheticBenchmark;
}

inline constexpr bool pearlKnownTargetSemantics(PearlTargetSemantics semantics) {
    return semantics == PearlTargetSemantics::BaseBeforePenalty ||
           semantics == PearlTargetSemantics::FinalAdjustedBound;
}

inline constexpr bool pearlKnownCertificate(PearlCertificateVersion version) {
    return version == PearlCertificateVersion::V1 ||
           version == PearlCertificateVersion::V2 ||
           version == PearlCertificateVersion::V3;
}

/** Public networks fail closed below the pinned V2 floor and on future certs. */
inline constexpr bool pearlCertificateAllowed(PearlNetwork network,
                                               uint64_t height,
                                               PearlCertificateVersion cert) {
    const uint64_t v2 = pearlV2Activation(network);
    const uint64_t v3 = pearlV3Activation(network);
    if (v2 == std::numeric_limits<uint64_t>::max() ||
        v3 == std::numeric_limits<uint64_t>::max() || height < v2)
        return false;
    return height < v3 ? cert == PearlCertificateVersion::V2
                       : cert == PearlCertificateVersion::V3;
}

inline constexpr bool pearlCertificateAllowedForContext(
    PearlNetwork network, uint64_t height, PearlCertificateVersion cert) {
    if (pearlPublicNetwork(network))
        return pearlCertificateAllowed(network, height, cert);
    if (network == PearlNetwork::Regtest)
        return height >= kPearlRegtestV3Activation &&
               cert == PearlCertificateVersion::V3;
    if (network == PearlNetwork::SyntheticBenchmark)
        return cert == PearlCertificateVersion::V1;
    return false;
}

inline const char *pearlNetworkToken(PearlNetwork network) {
    switch (network) {
    case PearlNetwork::Mainnet: return "mainnet";
    case PearlNetwork::Testnet: return "testnet";
    case PearlNetwork::Testnet2: return "testnet2";
    case PearlNetwork::Regtest: return "regtest";
    case PearlNetwork::SyntheticBenchmark: return "synthetic-benchmark";
    default: return nullptr;
    }
}

inline const char *pearlTargetSemanticsToken(PearlTargetSemantics semantics) {
    switch (semantics) {
    case PearlTargetSemantics::BaseBeforePenalty:
        return "base-before-penalty";
    case PearlTargetSemantics::FinalAdjustedBound:
        return "final-adjusted-bound";
    default: return nullptr;
    }
}

inline const char *pearlCertificateToken(PearlCertificateVersion version) {
    switch (version) {
    case PearlCertificateVersion::V1: return "v1";
    case PearlCertificateVersion::V2: return "v2";
    case PearlCertificateVersion::V3: return "v3";
    default: return nullptr;
    }
}

inline bool pearlSetContextError(PearlJobContextError value,
                                 PearlJobContextError *code,
                                 std::string *error,
                                 const char *message) noexcept {
    if (code) *code = value;
    if (error) {
        try {
            *error = message;
        } catch (...) {
            error->clear();
        }
    }
    return false;
}

// Deterministic host-test hook. Production builds contain no mutable seam.
#ifdef PEARL_JOB_CONTEXT_ENABLE_TEST_SEAMS
inline thread_local uint64_t pearlJobContextAllocationCountdownForTest =
    std::numeric_limits<uint64_t>::max();

inline void pearlSetJobContextAllocationCountdownForTest(
    uint64_t successfulCheckpoints) noexcept {
    pearlJobContextAllocationCountdownForTest = successfulCheckpoints;
}

inline void pearlDisableJobContextAllocationFailureForTest() noexcept {
    pearlJobContextAllocationCountdownForTest =
        std::numeric_limits<uint64_t>::max();
}

inline bool pearlJobContextAllocationCheckpoint() noexcept {
    if (pearlJobContextAllocationCountdownForTest ==
        std::numeric_limits<uint64_t>::max())
        return true;
    if (pearlJobContextAllocationCountdownForTest == 0) {
        pearlDisableJobContextAllocationFailureForTest();
        return false;
    }
    --pearlJobContextAllocationCountdownForTest;
    return true;
}
#else
inline bool pearlJobContextAllocationCheckpoint() noexcept { return true; }
#endif

inline bool pearlSyntheticHeader(const std::array<uint8_t, 76> &header) {
    uint8_t expected[kPearlBenchmarkHeaderBytes];
    fillPearlBenchmarkHeader(expected);
    return memcmp(header.data(), expected, header.size()) == 0;
}

inline bool pearlValidateContextFields(const PearlJobContext &context,
                                       PearlJobContextError *code,
                                       std::string *error) noexcept {
    if (!pearlKnownNetwork(context.network))
        return pearlSetContextError(PearlJobContextError::InvalidNetwork, code,
                                    error, "unknown Pearl network");
    if (!pearlKnownTargetSemantics(context.targetSemantics))
        return pearlSetContextError(
            PearlJobContextError::InvalidTargetSemantics, code, error,
            "unknown Pearl target semantics");
    if (!pearlKnownCertificate(context.certificateVersion))
        return pearlSetContextError(
            PearlJobContextError::InvalidCertificateVersion, code, error,
            "unknown Pearl certificate version");
    if (!pearlCertificateAllowedForContext(
            context.network, context.height, context.certificateVersion))
        return pearlSetContextError(PearlJobContextError::CertificateNotAllowed,
                                    code, error,
                                    "certificate is not allowed at this height");
    if (context.transportJobId.size() > kMaxPearlTransportJobIdBytes)
        return pearlSetContextError(PearlJobContextError::TransportJobIdTooLong,
                                    code, error,
                                    "Pearl transport job id is too long");
    if (context.network == PearlNetwork::SyntheticBenchmark &&
        (context.height != kPearlBenchmarkEpoch ||
         !context.transportJobId.empty() || !pearlSyntheticHeader(context.header)))
        return pearlSetContextError(PearlJobContextError::InvalidSyntheticContext,
                                    code, error,
                                    "invalid Pearl synthetic benchmark context");
    if (code) *code = PearlJobContextError::None;
    if (error) error->clear();
    return true;
}

inline void pearlAppendLowerHex(const uint8_t *bytes, size_t count,
                                std::string *out) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < count; ++i) {
        out->push_back(digits[bytes[i] >> 4]);
        out->push_back(digits[bytes[i] & 0x0f]);
    }
}

inline bool encodePearlJobContext(const PearlJobContext &context,
                                  std::string *extra, std::string *error,
                                  PearlJobContextError *code = nullptr) noexcept {
    if (!extra) {
        if (error == &context.transportJobId) {
            if (code) *code = PearlJobContextError::AliasedOutput;
            return false;
        }
        return pearlSetContextError(PearlJobContextError::NullOutput, code,
                                    error, "Pearl envelope output is null");
    }
    // Output aliases cannot satisfy both transactional input preservation and
    // the API's distinct success/error outputs. Refuse them before mutation.
    if (extra == error || extra == &context.transportJobId ||
        error == &context.transportJobId) {
        if (code) *code = PearlJobContextError::AliasedOutput;
        if (error && error != extra && error != &context.transportJobId) {
            try {
                *error = "Pearl encoder outputs alias input or each other";
            } catch (...) {
                error->clear();
            }
        }
        return false;
    }
    extra->clear();
    if (!pearlValidateContextFields(context, code, error)) return false;
    const char *network = pearlNetworkToken(context.network);
    const char *semantics = pearlTargetSemanticsToken(context.targetSemantics);
    const char *certificate =
        pearlCertificateToken(context.certificateVersion);
    try {
        if (!pearlJobContextAllocationCheckpoint())
            throw std::bad_alloc();
        std::string encoded;
        encoded.reserve(224 + context.transportJobId.size());
        encoded += "pearl-job-v1:";
        encoded += network;
        encoded += ':';
        encoded += semantics;
        encoded += ':';
        encoded += certificate;
        encoded += ':';
        pearlAppendLowerHex(context.header.data(), context.header.size(),
                            &encoded);
        encoded += ':';
        encoded += context.transportJobId;
        if (encoded.size() > kMaxPearlJobEnvelopeBytes)
            return pearlSetContextError(PearlJobContextError::EnvelopeTooLong,
                                        code, error,
                                        "Pearl envelope is too long");
        extra->swap(encoded);
    } catch (const std::bad_alloc &) {
        extra->clear();
        return pearlSetContextError(PearlJobContextError::AllocationFailure,
                                    code, error,
                                    "Pearl envelope allocation failed");
    } catch (const std::length_error &) {
        extra->clear();
        return pearlSetContextError(PearlJobContextError::AllocationFailure,
                                    code, error,
                                    "Pearl envelope allocation failed");
    }
    if (code) *code = PearlJobContextError::None;
    if (error) error->clear();
    return true;
}

inline bool pearlFieldEquals(const std::string &text, size_t begin, size_t end,
                             const char *token) {
    if (!token) return false;
    const size_t length = strlen(token);
    return end >= begin && end - begin == length &&
           text.compare(begin, length, token) == 0;
}

inline bool pearlParseNetwork(const std::string &text, size_t begin, size_t end,
                              PearlNetwork *network) {
    static constexpr PearlNetwork values[] = {
        PearlNetwork::Mainnet, PearlNetwork::Testnet, PearlNetwork::Testnet2,
        PearlNetwork::Regtest, PearlNetwork::SyntheticBenchmark};
    for (PearlNetwork value : values) {
        if (pearlFieldEquals(text, begin, end, pearlNetworkToken(value))) {
            *network = value;
            return true;
        }
    }
    return false;
}

inline bool pearlParseTargetSemantics(const std::string &text, size_t begin,
                                      size_t end,
                                      PearlTargetSemantics *semantics) {
    static constexpr PearlTargetSemantics values[] = {
        PearlTargetSemantics::BaseBeforePenalty,
        PearlTargetSemantics::FinalAdjustedBound};
    for (PearlTargetSemantics value : values) {
        if (pearlFieldEquals(text, begin, end,
                             pearlTargetSemanticsToken(value))) {
            *semantics = value;
            return true;
        }
    }
    return false;
}

inline bool pearlParseCertificate(const std::string &text, size_t begin,
                                  size_t end,
                                  PearlCertificateVersion *certificate) {
    static constexpr PearlCertificateVersion values[] = {
        PearlCertificateVersion::V1, PearlCertificateVersion::V2,
        PearlCertificateVersion::V3};
    for (PearlCertificateVersion value : values) {
        if (pearlFieldEquals(text, begin, end, pearlCertificateToken(value))) {
            *certificate = value;
            return true;
        }
    }
    return false;
}

inline bool pearlDecodeLowerHexHeader(const std::string &text, size_t begin,
                                      size_t end,
                                      std::array<uint8_t, 76> *header) {
    if (end - begin != header->size() * 2) return false;
    for (size_t i = 0; i < header->size(); ++i) {
        const char hi = text[begin + i * 2];
        const char lo = text[begin + i * 2 + 1];
        const auto digit = [](char c, uint8_t *out) {
            if (c >= '0' && c <= '9') {
                *out = static_cast<uint8_t>(c - '0');
                return true;
            }
            if (c >= 'a' && c <= 'f') {
                *out = static_cast<uint8_t>(c - 'a' + 10);
                return true;
            }
            return false;
        };
        uint8_t a = 0, b = 0;
        if (!digit(hi, &a) || !digit(lo, &b)) return false;
        (*header)[i] = static_cast<uint8_t>((a << 4) | b);
    }
    return true;
}

inline bool pearlContextErrorAliases(const Job &job,
                                     const PearlJobContext *context,
                                     const std::string *error) noexcept {
    return error &&
           (error == &job.extra || error == &job.jobId ||
            (context && error == &context->transportJobId));
}

inline bool decodePearlJobContext(const Job &job, PearlJobContext *context,
                                  std::string *error,
                                  PearlJobContextError *code = nullptr) noexcept {
    if (pearlContextErrorAliases(job, context, error)) {
        if (code) *code = PearlJobContextError::AliasedOutput;
        return false;
    }
    if (!context)
        return pearlSetContextError(PearlJobContextError::NullOutput, code,
                                    error, "Pearl context output is null");
    if (job.extra.size() > kMaxPearlJobEnvelopeBytes)
        return pearlSetContextError(PearlJobContextError::EnvelopeTooLong, code,
                                    error, "Pearl envelope is too long");

    const std::string &text = job.extra;
    size_t separators[5];
    size_t cursor = 0;
    for (size_t i = 0; i < 5; ++i) {
        separators[i] = text.find(':', cursor);
        if (separators[i] == std::string::npos)
            return pearlSetContextError(PearlJobContextError::MalformedEnvelope,
                                        code, error,
                                        "malformed Pearl job envelope");
        cursor = separators[i] + 1;
    }
    if (!pearlFieldEquals(text, 0, separators[0], "pearl-job-v1"))
        return pearlSetContextError(PearlJobContextError::MalformedEnvelope,
                                    code, error,
                                    "unknown Pearl envelope version");

    PearlJobContext parsed;
    parsed.height = job.epoch;
    if (!pearlParseNetwork(text, separators[0] + 1, separators[1],
                           &parsed.network))
        return pearlSetContextError(PearlJobContextError::InvalidNetwork, code,
                                    error, "unknown Pearl network token");
    if (!pearlParseTargetSemantics(text, separators[1] + 1, separators[2],
                                   &parsed.targetSemantics))
        return pearlSetContextError(
            PearlJobContextError::InvalidTargetSemantics, code, error,
            "unknown Pearl target-semantics token");
    if (!pearlParseCertificate(text, separators[2] + 1, separators[3],
                               &parsed.certificateVersion))
        return pearlSetContextError(
            PearlJobContextError::InvalidCertificateVersion, code, error,
            "unknown Pearl certificate token");
    if (!pearlDecodeLowerHexHeader(text, separators[3] + 1, separators[4],
                                   &parsed.header))
        return pearlSetContextError(PearlJobContextError::InvalidHeader, code,
                                    error,
                                    "Pearl header must be 152 lowercase hex characters");
    const size_t idBegin = separators[4] + 1;
    if (text.size() - idBegin > kMaxPearlTransportJobIdBytes)
        return pearlSetContextError(PearlJobContextError::TransportJobIdTooLong,
                                    code, error,
                                    "Pearl transport job id is too long");
    try {
        if (!pearlJobContextAllocationCheckpoint())
            throw std::bad_alloc();
        parsed.transportJobId.assign(text.data() + idBegin,
                                     text.size() - idBegin);
    } catch (const std::bad_alloc &) {
        return pearlSetContextError(PearlJobContextError::AllocationFailure,
                                    code, error,
                                    "Pearl context allocation failed");
    } catch (const std::length_error &) {
        return pearlSetContextError(PearlJobContextError::AllocationFailure,
                                    code, error,
                                    "Pearl context allocation failed");
    }
    if (!pearlValidateContextFields(parsed, code, error)) return false;

    std::string canonical;
    if (!encodePearlJobContext(parsed, &canonical, error, code)) return false;
    if (canonical != text)
        return pearlSetContextError(PearlJobContextError::NonCanonicalEnvelope,
                                    code, error,
                                    "non-canonical Pearl job envelope");
    *context = std::move(parsed);
    if (code) *code = PearlJobContextError::None;
    if (error) error->clear();
    return true;
}

inline bool pearlJobContextEqual(const PearlJobContext &a,
                                 const PearlJobContext &b) {
    return a.network == b.network &&
           a.targetSemantics == b.targetSemantics && a.height == b.height &&
           a.certificateVersion == b.certificateVersion &&
           a.header == b.header && a.transportJobId == b.transportJobId;
}

inline bool pearlTargetIsZero(const uint64_t target[4]) {
    return (target[0] | target[1] | target[2] | target[3]) == 0;
}

/** H-025 D6, ported from live pearl_pool_target.py:compact_floor. A v2 pool's
 * raw notify target is floored toward zero to the three-byte compact mantissa
 * that the official verifier reads. `raw` and `out` use limb 0 = least
 * significant. Returns false for a zero target or a value that cannot be
 * floored. Used only for the resolved bound; the raw target keeps the job identity. */
inline bool pearlPoolCompactFloor(const uint64_t raw[4], uint64_t out[4]) {
    uint8_t be[32];
    for (int i = 0; i < 4; i++)
        for (int b = 0; b < 8; b++)
            be[31 - (i * 8 + b)] = static_cast<uint8_t>(raw[i] >> (8 * b));
    int first = 0;
    while (first < 32 && be[first] == 0) first++;
    if (first == 32) return false;
    // size = significant bytes; word = top three bytes, zero-padded past the end.
    int size = 32 - first;
    uint8_t top[3];
    for (int k = 0; k < 3; k++) top[k] = first + k < 32 ? be[first + k] : 0;
    uint32_t word = (uint32_t(top[0]) << 16) | (uint32_t(top[1]) << 8) | top[2];
    if (word & 0x800000u) {
        word >>= 8;
        size++;
    }
    // effective = word << 8*(size-3) for size >= 3, else word >> 8*(3-size).
    // Its big-endian bytes are the leading `size` bytes of word then zeros,
    // placed right-aligned in a 40-byte buffer so 33-byte sizes are caught.
    const uint8_t wordBytes[3] = {static_cast<uint8_t>(word >> 16),
                                  static_cast<uint8_t>(word >> 8),
                                  static_cast<uint8_t>(word)};
    uint8_t effective[40] = {};
    for (int j = 0; j < size; j++)
        effective[40 - size + j] = j < 3 ? wordBytes[j] : 0;
    for (int i = 0; i < 8; i++)
        if (effective[i]) return false;
    for (int i = 0; i < 4; i++) {
        uint64_t limb = 0;
        for (int b = 0; b < 8; b++)
            limb |= static_cast<uint64_t>(effective[39 - (i * 8 + b)]) << (8 * b);
        out[i] = limb;
    }
    // The floor must never ease the target (live: 0 < effective <= target).
    return !pearlTargetIsZero(out) &&
           pearl::U256::fromLimbs(out).le(pearl::U256::fromLimbs(raw));
}

inline bool validatePearlJobContext(
    const Job &job, const PearlSourcePolicy *expected,
    PearlJobContext *context, std::string *error,
    PearlJobContextError *code = nullptr) noexcept {
    if (pearlContextErrorAliases(job, context, error)) {
        if (code) *code = PearlJobContextError::AliasedOutput;
        return false;
    }
    if (!context)
        return pearlSetContextError(PearlJobContextError::NullOutput, code,
                                    error, "Pearl context output is null");
    if (!job.valid)
        return pearlSetContextError(PearlJobContextError::InvalidJob, code,
                                    error, "Pearl job is not valid");
    PearlJobContext parsed;
    if (!decodePearlJobContext(job, &parsed, error, code)) return false;
    if (job.jobId != parsed.transportJobId)
        return pearlSetContextError(PearlJobContextError::ContextMismatch,
                                    code, error,
                                    "Pearl transport job id does not match Job::jobId");
    uint8_t digest[32];
    pearl::b3::hash(nullptr, parsed.header.data(), parsed.header.size(), digest);
    if (memcmp(digest, job.msg, sizeof(digest)) != 0)
        return pearlSetContextError(PearlJobContextError::HeaderMessageMismatch,
                                    code, error,
                                    "Pearl header does not match Job::msg");
    if (expected) {
        if (!pearlKnownNetwork(expected->network) ||
            !pearlKnownTargetSemantics(expected->targetSemantics) ||
            parsed.network != expected->network ||
            parsed.targetSemantics != expected->targetSemantics)
            return pearlSetContextError(PearlJobContextError::PolicyMismatch,
                                        code, error,
                                        "Pearl source policy mismatch");
    }
    if (pearlTargetIsZero(job.target) &&
        parsed.network != PearlNetwork::SyntheticBenchmark)
        return pearlSetContextError(PearlJobContextError::ZeroTarget, code,
                                    error,
                                    "zero target is allowed only for synthetic benchmark");
    if (!pearlTargetIsZero(job.target) &&
        parsed.network == PearlNetwork::SyntheticBenchmark)
        return pearlSetContextError(PearlJobContextError::InvalidSyntheticContext,
                                    code, error,
                                    "synthetic benchmark target must be zero");
    *context = std::move(parsed);
    if (code) *code = PearlJobContextError::None;
    if (error) error->clear();
    return true;
}

/**
 * Build the one canonical, non-submittable benchmark fixture. Unlike decoders,
 * this builder has no input Job to preserve: every failure resets its output,
 * including a diagnostic alias into that output. Never put a diagnostic into
 * Job::extra or Job::jobId; both must remain empty on failure.
 */
inline bool buildPearlSyntheticBenchmarkJob(
    Job *job, std::string *error = nullptr,
    PearlJobContextError *code = nullptr) noexcept {
    if (!job)
        return pearlSetContextError(PearlJobContextError::NullOutput, code,
                                    error, "Pearl benchmark job output is null");
    const bool errorAliases = error == &job->extra || error == &job->jobId;
    job->valid = false;
    memset(job->msg, 0, sizeof(job->msg));
    memset(job->target, 0, sizeof(job->target));
    job->epoch = 0;
    job->extra.clear();
    job->jobId.clear();
    if (errorAliases) {
        if (code) *code = PearlJobContextError::AliasedOutput;
        return false;
    }
    try {
        PearlJobContext context;
        context.network = PearlNetwork::SyntheticBenchmark;
        context.targetSemantics = PearlTargetSemantics::FinalAdjustedBound;
        context.height = kPearlBenchmarkEpoch;
        context.certificateVersion =
            static_cast<PearlCertificateVersion>(kPearlBenchmarkCertificate);
        uint8_t header[kPearlBenchmarkHeaderBytes];
        fillPearlBenchmarkHeader(header);
        memcpy(context.header.data(), header, sizeof(header));
        Job next{};
        next.epoch = context.height;
        if (!encodePearlJobContext(context, &next.extra, error, code))
            return false;
        pearl::b3::hash(nullptr, context.header.data(), context.header.size(),
                        next.msg);
        next.valid = true;
        const PearlSourcePolicy policy{context.network,
                                       context.targetSemantics};
        PearlJobContext validated;
        if (!validatePearlJobContext(next, &policy, &validated, error, code))
            return false;
        // All potentially allocating validation is complete. Publish without
        // allocation and make validity the final field written.
        memcpy(job->msg, next.msg, sizeof(job->msg));
        job->epoch = next.epoch;
        job->extra.swap(next.extra);
        job->valid = true;
        if (code) *code = PearlJobContextError::None;
        if (error) error->clear();
        return true;
    } catch (...) {
        return pearlSetContextError(PearlJobContextError::AllocationFailure,
                                    code, error,
                                    "Pearl benchmark job construction failed");
    }
}

inline bool pearlMiningConfigValidForTarget(const pearl::MiningConfig &config) {
    const uint64_t rank = config.rank;
    const uint64_t width = config.commonDim;
    if (rank < pearl::kPenaltyBaseRank || rank > 1024 ||
        (rank & (rank - 1)) != 0 || rank % 16 != 0)
        return false;
    const uint64_t maxWidth =
        (4 * rank * rank < 65536) ? 4 * rank * rank : 65536;
    if (width % 64 != 0 || width < 1024 || width < 16 * rank ||
        width > maxWidth)
        return false;
    if (config.tileH <= 0 || config.tileW <= 0 || config.tileH % 2 != 0 ||
        config.tileW % 2 != 0)
        return false;
    const uint64_t tile = static_cast<uint64_t>(config.tileH) *
                          static_cast<uint64_t>(config.tileW);
    return tile >= 32 && tile <= 256;
}

inline bool resolvePearlTarget(const Job &job,
                               const PearlJobContext &context,
                               const pearl::MiningConfig &config,
                               pearl::U256 *finalBound, std::string *error,
                               PearlJobContextError *code = nullptr) noexcept {
    if (!finalBound)
        return pearlSetContextError(PearlJobContextError::NullOutput, code,
                                    error, "Pearl target output is null");
    PearlJobContext decoded;
    if (!validatePearlJobContext(job, nullptr, &decoded, error, code))
        return false;
    if (!pearlJobContextEqual(decoded, context))
        return pearlSetContextError(PearlJobContextError::ContextMismatch,
                                    code, error,
                                    "Pearl target context does not match job");

    const pearl::U256 sourceTarget = pearl::U256::fromLimbs(job.target);
    pearl::U256 resolved;
    if (context.targetSemantics == PearlTargetSemantics::BaseBeforePenalty) {
        const uint64_t activation =
            pearlRankPenaltyActivation(context.network);
        const bool penaltyActive =
            activation != std::numeric_limits<uint64_t>::max() &&
            context.height >= activation;
        if (penaltyActive) {
            if (!pearlMiningConfigValidForTarget(config))
                return pearlSetContextError(
                    PearlJobContextError::InvalidMiningConfig, code, error,
                    "invalid Pearl mining configuration");
            if (!config.penalizedTarget(sourceTarget, &resolved))
                return pearlSetContextError(
                    PearlJobContextError::TargetOverflow, code, error,
                    "Pearl rank penalty overflows target");
        } else {
            resolved = sourceTarget;
        }
    } else if (context.targetSemantics ==
               PearlTargetSemantics::FinalAdjustedBound) {
        resolved = sourceTarget;
    } else {
        return pearlSetContextError(
            PearlJobContextError::InvalidTargetSemantics, code, error,
            "unknown Pearl target semantics");
    }
    *finalBound = resolved;
    if (code) *code = PearlJobContextError::None;
    if (error) error->clear();
    return true;
}

}  // namespace om
