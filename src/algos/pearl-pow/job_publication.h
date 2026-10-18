// Pure-host preparation and fail-closed publication for Pearl per-job state.
//
// Protocol and fork semantics come exclusively from pearl_job_context.h. This
// file contains no CUDA or Vulkan types and is not wired into either backend.

#pragma once

#include <stdint.h>

#include <array>
#include <cstring>
#include <type_traits>

#include "../../core/pearl_job_context.h"

namespace om {
namespace pearl {

enum class PreparedJobMode : uint8_t {
    Normal = 1,
    SyntheticHardTarget = 2,
};

enum class JobPublicationState : uint8_t {
    Empty = 0,
    Ready,
    Dirty,
    Poisoned,
};

inline constexpr uint32_t kPreparedJobSchemaVersion = 2;

/** Backend choices which are not protocol fields but change device fixtures. */
struct PreparedJobRequest {
    MiningConfig config{0, 0, 0, 0};
    uint32_t m = 0;
    uint32_t n = 0;
    uint64_t preparationPlanId = 0;
    PreparedJobMode mode = PreparedJobMode::Normal;
};

/**
 * Fully derived computational state for one device preparation.
 *
 * transportJobId is intentionally absent. It does not change the matrices,
 * target, commitments or proof bytes; exact proof ownership binds it outside
 * this computational cache.
 */
struct PreparedJobHost {
    uint32_t schemaVersion = 0;
    PearlNetwork network = static_cast<PearlNetwork>(0);
    uint64_t height = 0;
    PearlTargetSemantics targetSemantics =
        static_cast<PearlTargetSemantics>(0);
    PearlCertificateVersion certificateVersion =
        static_cast<PearlCertificateVersion>(0);
    std::array<uint8_t, 76> header{};
    std::array<uint64_t, 4> sourceTarget{};
    MiningConfig config{0, 0, 0, 0};
    uint32_t m = 0;
    uint32_t n = 0;
    uint64_t preparationPlanId = 0;
    PreparedJobMode mode = static_cast<PreparedJobMode>(0);
    bool compactFloorTarget = false; // H-025 D6: resolvedBound is from the floored target
    U256 resolvedBound{};
    std::array<uint8_t, 32> jobKeyBytes{};
    std::array<uint8_t, 32> seedABytes{};
    std::array<uint8_t, 32> seedBBytes{};
    std::array<uint8_t, 32> targetBytesLE{};
    uint64_t jobSalt = 0;
    uint64_t btSeed = 0;

    void clear() noexcept { *this = PreparedJobHost{}; }

    bool initialized() const noexcept {
        return schemaVersion == kPreparedJobSchemaVersion;
    }
};

static_assert(std::is_nothrow_copy_assignable<PreparedJobHost>::value,
              "prepared Pearl state must publish without exceptions");
static_assert(std::is_trivially_copyable<PreparedJobHost>::value,
              "prepared Pearl state must remain a fixed-size POD record");
static_assert(std::is_standard_layout<PreparedJobHost>::value,
              "prepared Pearl state must remain standard-layout");

inline bool preparedConfigEqual(const MiningConfig &a,
                                const MiningConfig &b) noexcept {
    return a.commonDim == b.commonDim && a.rank == b.rank &&
           a.tileH == b.tileH && a.tileW == b.tileW;
}

inline bool preparedU256Equal(const U256 &a, const U256 &b) noexcept {
    return std::memcmp(a.v, b.v, sizeof(a.v)) == 0;
}

inline bool operator==(const PreparedJobHost &a,
                       const PreparedJobHost &b) noexcept {
    return a.schemaVersion == b.schemaVersion && a.network == b.network &&
           a.height == b.height && a.targetSemantics == b.targetSemantics &&
           a.certificateVersion == b.certificateVersion &&
           a.header == b.header && a.sourceTarget == b.sourceTarget &&
           preparedConfigEqual(a.config, b.config) && a.m == b.m &&
           a.n == b.n && a.preparationPlanId == b.preparationPlanId &&
           a.mode == b.mode && a.compactFloorTarget == b.compactFloorTarget &&
           preparedU256Equal(a.resolvedBound, b.resolvedBound) &&
           a.jobKeyBytes == b.jobKeyBytes &&
           a.seedABytes == b.seedABytes &&
           a.seedBBytes == b.seedBBytes &&
           a.targetBytesLE == b.targetBytesLE && a.jobSalt == b.jobSalt &&
           a.btSeed == b.btSeed;
}

inline bool operator!=(const PreparedJobHost &a,
                       const PreparedJobHost &b) noexcept {
    return !(a == b);
}

/**
 * Allocation-free equivalent of MiningConfig::check(m, n): canonical
 * rank/k/tile rules followed by the two shape-only rules.
 */
inline bool preparedMiningShapeValid(const MiningConfig &cfg, uint32_t m,
                                     uint32_t n) noexcept {
    if (!pearlMiningConfigValidForTarget(cfg)) return false;
    if (m > (1u << 24) || n > (1u << 24)) return false;
    return static_cast<uint32_t>(cfg.tileH) <= m &&
           static_cast<uint32_t>(cfg.tileW) <= n;
}

inline uint64_t preparedLoadU64LE(const uint8_t bytes[8]) noexcept {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
    return value;
}

/** Validate canonical context and resolve its fork-aware target first. */
inline bool buildPreparedJobHost(
    const Job &job, const PearlSourcePolicy *expected,
    const PreparedJobRequest &request, PreparedJobHost *out,
    std::string *error = nullptr,
    PearlJobContextError *code = nullptr) noexcept {
    if (!out)
        return pearlSetContextError(PearlJobContextError::NullOutput, code,
                                    error, "Pearl prepared-job output is null");
    out->clear();

    PearlJobContext context;
    if (!validatePearlJobContext(job, expected, &context, error, code))
        return false;
    // Canonical validation binds these duplicate transport representations.
    // Retain the local assertion so this publication boundary still refuses
    // ambiguity if that lower-level contract is ever weakened, before the id
    // is deliberately dropped from computational cache identity.
    if (job.jobId != context.transportJobId)
        return pearlSetContextError(
            PearlJobContextError::ContextMismatch, code, error,
            "generic and Pearl-envelope transport job ids differ");

    // H-025 D6: a v2 session's GPU bound uses the compact-floored target. The
    // raw target is still what sourceTarget records below (identity).
    Job resolvable = job;
    if (job.compactFloorTarget &&
        !pearlPoolCompactFloor(job.target, resolvable.target))
        return pearlSetContextError(PearlJobContextError::InvalidJob, code,
                                    error,
                                    "Pearl target cannot be compact-floored");
    U256 resolved;
    if (!resolvePearlTarget(resolvable, context, request.config, &resolved,
                            error, code))
        return false;

    if (!preparedMiningShapeValid(request.config, request.m, request.n))
        return pearlSetContextError(
            PearlJobContextError::InvalidMiningConfig, code, error,
            "invalid Pearl mining configuration or device shape");
    if (request.preparationPlanId == 0)
        return pearlSetContextError(
            PearlJobContextError::InvalidMiningConfig, code, error,
            "Pearl preparation plan id must be nonzero");

    if (request.mode != PreparedJobMode::Normal &&
        request.mode != PreparedJobMode::SyntheticHardTarget)
        return pearlSetContextError(PearlJobContextError::InvalidSyntheticContext,
                                    code, error,
                                    "unknown Pearl preparation mode");
    if (request.mode == PreparedJobMode::SyntheticHardTarget &&
        context.network != PearlNetwork::SyntheticBenchmark)
        return pearlSetContextError(
            PearlJobContextError::InvalidSyntheticContext, code, error,
            "hard target is restricted to synthetic benchmark context");

    PreparedJobHost next;
    next.schemaVersion = kPreparedJobSchemaVersion;
    next.network = context.network;
    next.height = context.height;
    next.targetSemantics = context.targetSemantics;
    next.certificateVersion = context.certificateVersion;
    next.header = context.header;
    std::memcpy(next.sourceTarget.data(), job.target, sizeof(job.target));
    next.config = request.config;
    next.m = request.m;
    next.n = request.n;
    next.preparationPlanId = request.preparationPlanId;
    next.mode = request.mode;
    next.compactFloorTarget = job.compactFloorTarget;
    next.resolvedBound = resolved;
    // Canonical synthetic jobs already require a zero source target. Apply
    // the diagnostic override only after canonical validation and resolution.
    if (request.mode == PreparedJobMode::SyntheticHardTarget)
        next.resolvedBound = U256{};

    jobKey(next.header.data(), next.config, next.jobKeyBytes.data());
    seedForA(next.seedABytes.data());
    seedForB(next.seedBBytes.data());
    next.resolvedBound.toBytesLE(next.targetBytesLE.data());
    next.jobSalt = preparedLoadU64LE(next.jobKeyBytes.data());
    next.btSeed = matrixSeed(next.jobSalt, true);

    *out = next;
    if (code) *code = PearlJobContextError::None;
    if (error) error->clear();
    return true;
}

/** Re-run canonical construction so hand-assembled state cannot be published. */
inline bool preparedJobHostConsistent(const PreparedJobHost &candidate) noexcept {
    if (!candidate.initialized()) return false;

    PearlJobContext context;
    context.network = candidate.network;
    context.targetSemantics = candidate.targetSemantics;
    context.height = candidate.height;
    context.certificateVersion = candidate.certificateVersion;
    context.header = candidate.header;
    // Deliberately empty: transport identity is not computational identity.
    context.transportJobId.clear();

    Job job;
    if (!encodePearlJobContext(context, &job.extra, nullptr)) return false;
    b3::hash(nullptr, context.header.data(), context.header.size(), job.msg);
    std::memcpy(job.target, candidate.sourceTarget.data(), sizeof(job.target));
    job.epoch = candidate.height;
    job.valid = true;
    job.compactFloorTarget = candidate.compactFloorTarget;

    PreparedJobRequest request;
    request.config = candidate.config;
    request.m = candidate.m;
    request.n = candidate.n;
    request.preparationPlanId = candidate.preparationPlanId;
    request.mode = candidate.mode;
    PreparedJobHost rebuilt;
    return buildPreparedJobHost(job, nullptr, request, &rebuilt) &&
           rebuilt == candidate;
}

/** Fail-closed, single-owner publication state for a backend's prepared job. */
class JobPublication {
  public:
    JobPublication() noexcept = default;
    JobPublication(const JobPublication &) = delete;
    JobPublication &operator=(const JobPublication &) = delete;
    JobPublication(JobPublication &&) = delete;
    JobPublication &operator=(JobPublication &&) = delete;

    JobPublicationState state() const noexcept { return state_; }
    bool ready() const noexcept { return state_ == JobPublicationState::Ready; }
    bool poisoned() const noexcept {
        return state_ == JobPublicationState::Poisoned;
    }

    bool matches(const PreparedJobHost &candidate) const noexcept {
        return ready() && candidate.initialized() && committed_ == candidate;
    }

    bool beginRewrite() noexcept {
        if (poisoned()) return false;
        committed_.clear();
        state_ = JobPublicationState::Dirty;
        return true;
    }

    void abandonRetryable() noexcept {
        if (poisoned()) return;
        committed_.clear();
        state_ = JobPublicationState::Dirty;
    }

    void poison() noexcept {
        committed_.clear();
        state_ = JobPublicationState::Poisoned;
    }

    bool publish(const PreparedJobHost &candidate) noexcept {
        if (state_ != JobPublicationState::Dirty ||
            !preparedJobHostConsistent(candidate))
            return false;
        committed_ = candidate;
        state_ = JobPublicationState::Ready;  // deliberately last
        return true;
    }

    bool readReady(PreparedJobHost *out) const noexcept {
        if (!out) return false;
        out->clear();
        if (!ready()) return false;
        *out = committed_;
        return true;
    }

    void resetAfterDeviceRecreate() noexcept {
        committed_.clear();
        state_ = JobPublicationState::Empty;
    }

  private:
    JobPublicationState state_ = JobPublicationState::Empty;
    PreparedJobHost committed_{};
};

}  // namespace pearl
}  // namespace om
