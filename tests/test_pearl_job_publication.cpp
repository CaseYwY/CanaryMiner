#include <stdint.h>

#include <array>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

#define PEARL_JOB_CONTEXT_ENABLE_TEST_SEAMS 1
#include "../src/algos/pearl-pow/job_publication.h"

namespace {

using namespace om;
using pearl::JobPublication;
using pearl::JobPublicationState;
using pearl::PreparedJobHost;
using pearl::PreparedJobMode;
using pearl::PreparedJobRequest;

int failures = 0;

void check(bool condition, const char *name) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

void setContext(
    PearlJobContext *context,
    PearlNetwork network, uint64_t height,
    PearlCertificateVersion certificate,
    PearlTargetSemantics semantics = PearlTargetSemantics::BaseBeforePenalty,
    const std::string &transportId = "pool-job-A") {
    context->network = network;
    context->targetSemantics = semantics;
    context->height = height;
    context->certificateVersion = certificate;
    context->transportJobId = transportId;
    if (network == PearlNetwork::SyntheticBenchmark) {
        context->transportJobId.clear();
        uint8_t header[kPearlBenchmarkHeaderBytes];
        fillPearlBenchmarkHeader(header);
        std::memcpy(context->header.data(), header, sizeof(header));
    } else {
        for (size_t i = 0; i < context->header.size(); ++i)
            context->header[i] = static_cast<uint8_t>(i * 37u + 11u);
    }
}

Job jobFor(const PearlJobContext &context,
           const std::array<uint64_t, 4> &target) {
    Job job;
    std::string error;
    PearlJobContextError code = PearlJobContextError::None;
    check(encodePearlJobContext(context, &job.extra, &error, &code),
          "test context encodes");
    pearl::b3::hash(nullptr, context.header.data(), context.header.size(),
                    job.msg);
    std::memcpy(job.target, target.data(), sizeof(job.target));
    job.epoch = context.height;
    job.jobId = context.transportJobId;
    job.valid = true;
    return job;
}

std::array<uint64_t, 4> baseTarget() {
    return {UINT64_C(0x0123456789abcdef),
            UINT64_C(0x0011223344556677),
            UINT64_C(0x0001020304050607), UINT64_C(3)};
}

PreparedJobRequest baseRequest() {
    PreparedJobRequest request;
    request.config.commonDim = 2048;
    request.config.rank = 128;
    request.config.tileH = 16;
    request.config.tileW = 16;
    request.m = 4096;
    request.n = 16384;
    request.preparationPlanId = UINT64_C(0x435544412d424e54);
    request.mode = PreparedJobMode::Normal;
    return request;
}

Job baseJob(const std::string &transportId = "pool-job-A") {
    PearlJobContext context;
    setContext(&context, PearlNetwork::Mainnet, kPearlMainnetV3Activation,
               PearlCertificateVersion::V3,
               PearlTargetSemantics::BaseBeforePenalty, transportId);
    return jobFor(context, baseTarget());
}

PreparedJobHost build(const Job &job, const PreparedJobRequest &request,
                      const char *name,
                      const PearlSourcePolicy *expected = nullptr) {
    PreparedJobHost out;
    std::string error;
    PearlJobContextError code = PearlJobContextError::None;
    check(pearl::buildPreparedJobHost(job, expected, request, &out, &error,
                                      &code),
          name);
    check(out.initialized(), "successful build is initialized");
    check(code == PearlJobContextError::None && error.empty(),
          "successful build clears error outputs");
    return out;
}

void expectBuildFailure(const Job &job, const PreparedJobRequest &request,
                        PearlJobContextError expectedCode, const char *name,
                        const PearlSourcePolicy *policy = nullptr) {
    PreparedJobHost out = build(baseJob(), baseRequest(),
                                "failure sentinel builds");
    std::string error;
    PearlJobContextError code = PearlJobContextError::None;
    check(!pearl::buildPreparedJobHost(job, policy, request, &out, &error,
                                       &code),
          name);
    check(out == PreparedJobHost{}, "failed build zeroes output");
    check(code == expectedCode && !error.empty(),
          "failed build reports canonical error");
}

void testCanonicalForkBoundaries() {
    struct PublicBoundary {
        PearlNetwork network;
        uint64_t v2;
        uint64_t penalty;
        uint64_t v3;
    };
    static constexpr PublicBoundary boundaries[] = {
        {PearlNetwork::Mainnet, kPearlMainnetV2Activation,
         kPearlMainnetRankPenaltyActivation, kPearlMainnetV3Activation},
        {PearlNetwork::Testnet, kPearlTestnetV2Activation,
         kPearlTestnetRankPenaltyActivation, kPearlTestnetV3Activation},
        {PearlNetwork::Testnet2, kPearlTestnet2V2Activation,
         kPearlTestnet2RankPenaltyActivation, kPearlTestnet2V3Activation},
    };

    const std::array<uint64_t, 4> smallTarget{5, 0, 0, 0};
    const PreparedJobRequest request = baseRequest();
    pearl::U256 penalized;
    check(request.config.penalizedTarget(
                                         pearl::U256::fromLimbs(smallTarget.data()),
                                         &penalized),
          "test penalty is representable");

    for (const PublicBoundary &boundary : boundaries) {
        PearlJobContext atV2;
        setContext(&atV2, boundary.network, boundary.v2,
                   PearlCertificateVersion::V2);
        Job belowV2 = jobFor(atV2, smallTarget);
        belowV2.epoch = boundary.v2 - 1;
        expectBuildFailure(belowV2, request,
                           PearlJobContextError::CertificateNotAllowed,
                           "below V2 activation is refused");
        PreparedJobHost v2 = build(jobFor(atV2, smallTarget), request,
                                   "V2 activation boundary builds");
        check(v2.certificateVersion == PearlCertificateVersion::V2,
              "V2 boundary retains canonical certificate");

        PearlJobContext beforeV3;
        setContext(&beforeV3, boundary.network, boundary.v3 - 1,
                   PearlCertificateVersion::V2);
        PreparedJobHost lastV2 = build(jobFor(beforeV3, smallTarget), request,
                                       "last pre-V3 V2 job builds");
        check(lastV2.certificateVersion == PearlCertificateVersion::V2,
              "V2 remains valid immediately before V3 activation");
        Job v3TooEarly = jobFor(beforeV3, smallTarget);
        v3TooEarly.epoch = boundary.v3;
        // Its envelope still says V2, which must be refused at the V3 boundary.
        expectBuildFailure(v3TooEarly, request,
                           PearlJobContextError::CertificateNotAllowed,
                           "V2 is refused at V3 activation");
        PearlJobContext atV3;
        setContext(&atV3, boundary.network, boundary.v3,
                   PearlCertificateVersion::V3);
        Job v3Before = jobFor(atV3, smallTarget);
        v3Before.epoch = boundary.v3 - 1;
        expectBuildFailure(v3Before, request,
                           PearlJobContextError::CertificateNotAllowed,
                           "V3 is refused before activation");
        PreparedJobHost v3 = build(jobFor(atV3, smallTarget), request,
                                   "V3 activation boundary builds");
        check(v3.certificateVersion == PearlCertificateVersion::V3,
              "V3 boundary retains canonical certificate");

        PearlJobContext prePenalty;
        setContext(&prePenalty, boundary.network, boundary.penalty - 1,
                   PearlCertificateVersion::V2);
        PreparedJobHost before = build(jobFor(prePenalty, smallTarget), request,
                                       "pre-penalty job builds");
        check(pearl::preparedU256Equal(
                  before.resolvedBound,
                  pearl::U256::fromLimbs(smallTarget.data())),
              "pre-activation base target is not penalized");
        PearlJobContext atPenalty;
        setContext(&atPenalty, boundary.network, boundary.penalty,
                   PearlCertificateVersion::V2);
        PreparedJobHost after = build(jobFor(atPenalty, smallTarget), request,
                                      "penalty activation job builds");
        check(pearl::preparedU256Equal(after.resolvedBound, penalized),
              "activation-height base target is penalized exactly once");

        PearlJobContext finalAtPenalty;
        setContext(&finalAtPenalty, boundary.network, boundary.penalty,
                   PearlCertificateVersion::V2,
                   PearlTargetSemantics::FinalAdjustedBound);
        PreparedJobHost final = build(jobFor(finalAtPenalty, smallTarget),
                                      request, "final target job builds");
        check(pearl::preparedU256Equal(
                  final.resolvedBound,
                  pearl::U256::fromLimbs(smallTarget.data())),
              "final-adjusted target is never penalized again");
    }

    PearlJobContext regtest;
    setContext(&regtest, PearlNetwork::Regtest, 1,
               PearlCertificateVersion::V3);
    Job regtestBefore = jobFor(regtest, smallTarget);
    regtestBefore.epoch = 0;
    expectBuildFailure(regtestBefore, request,
                       PearlJobContextError::CertificateNotAllowed,
                       "regtest pre-activation height is refused");
    PreparedJobHost regtestAt = build(jobFor(regtest, smallTarget), request,
                                      "regtest activation builds");
    check(pearl::preparedU256Equal(regtestAt.resolvedBound, penalized),
          "regtest applies penalty at its activation height");
}

void testCanonicalValidationAndSyntheticMode() {
    const PreparedJobRequest request = baseRequest();
    Job invalid = baseJob();
    invalid.valid = false;
    expectBuildFailure(invalid, request, PearlJobContextError::InvalidJob,
                       "invalid generic job is refused");

    Job unknownNetwork = baseJob();
    unknownNetwork.extra.replace(unknownNetwork.extra.find(":mainnet:"),
                                 sizeof(":mainnet:") - 1, ":future:");
    expectBuildFailure(unknownNetwork, request,
                       PearlJobContextError::InvalidNetwork,
                       "unknown canonical network is refused");
    Job unknownSemantics = baseJob();
    unknownSemantics.extra.replace(
        unknownSemantics.extra.find(":base-before-penalty:"),
        sizeof(":base-before-penalty:") - 1, ":unknown-target:");
    expectBuildFailure(unknownSemantics, request,
                       PearlJobContextError::InvalidTargetSemantics,
                       "unknown canonical target semantics is refused");
    Job unknownCertificate = baseJob();
    unknownCertificate.extra.replace(unknownCertificate.extra.find(":v3:"),
                                     sizeof(":v3:") - 1, ":v4:");
    expectBuildFailure(unknownCertificate, request,
                       PearlJobContextError::InvalidCertificateVersion,
                       "unknown canonical certificate is refused");

    Job mismatch = baseJob();
    mismatch.msg[7] ^= 1;
    expectBuildFailure(mismatch, request,
                       PearlJobContextError::HeaderMessageMismatch,
                       "header/message mismatch is refused");
    Job transportMismatch = baseJob();
    transportMismatch.jobId = "different-generic-transport-id";
    expectBuildFailure(transportMismatch, request,
                       PearlJobContextError::ContextMismatch,
                       "generic/envelope transport id mismatch is refused");

    PearlSourcePolicy wrongNetwork{PearlNetwork::Testnet,
                                   PearlTargetSemantics::BaseBeforePenalty};
    expectBuildFailure(baseJob(), request, PearlJobContextError::PolicyMismatch,
                       "network policy mismatch is refused", &wrongNetwork);
    PearlSourcePolicy wrongSemantics{
        PearlNetwork::Mainnet, PearlTargetSemantics::FinalAdjustedBound};
    expectBuildFailure(baseJob(), request, PearlJobContextError::PolicyMismatch,
                       "target policy mismatch is refused", &wrongSemantics);

    Job publicZero = baseJob();
    std::memset(publicZero.target, 0, sizeof(publicZero.target));
    expectBuildFailure(publicZero, request, PearlJobContextError::ZeroTarget,
                       "public zero target is refused");

    Job oversizedEnvelope = baseJob();
    oversizedEnvelope.extra.assign(kMaxPearlJobEnvelopeBytes + 1, 'x');
    expectBuildFailure(oversizedEnvelope, request,
                       PearlJobContextError::EnvelopeTooLong,
                       "over-limit canonical envelope is refused");
    Job oversizedTransport = baseJob();
    const size_t idBegin = oversizedTransport.extra.rfind(':') + 1;
    oversizedTransport.extra.erase(idBegin);
    oversizedTransport.extra.append(kMaxPearlTransportJobIdBytes + 1, 'x');
    expectBuildFailure(oversizedTransport, request,
                       PearlJobContextError::TransportJobIdTooLong,
                       "over-limit transport id is refused");

    PearlJobContext synthetic;
    setContext(&synthetic, PearlNetwork::SyntheticBenchmark,
               kPearlBenchmarkEpoch, PearlCertificateVersion::V1);
    const std::array<uint64_t, 4> zeroTarget{};
    Job syntheticJob = jobFor(synthetic, zeroTarget);
    PreparedJobHost normal = build(syntheticJob, request,
                                   "canonical synthetic job builds");
    check(normal.network == PearlNetwork::SyntheticBenchmark &&
              normal.certificateVersion == PearlCertificateVersion::V1,
          "synthetic typed context is retained");
    PreparedJobRequest hard = request;
    hard.mode = PreparedJobMode::SyntheticHardTarget;
    PreparedJobHost hardPrepared = build(syntheticJob, hard,
                                         "synthetic hard-target job builds");
    check(hardPrepared.mode == PreparedJobMode::SyntheticHardTarget &&
              hardPrepared.resolvedBound.v[0] == 0 &&
              hardPrepared.resolvedBound.v[3] == 0,
          "hard mode applies only after canonical zero-target resolution");
    check(hardPrepared != normal, "diagnostic mode is cache identity");

    expectBuildFailure(baseJob(), hard,
                       PearlJobContextError::InvalidSyntheticContext,
                       "hard mode is refused for public work");
    Job syntheticNonzero = syntheticJob;
    syntheticNonzero.target[0] = 1;
    expectBuildFailure(syntheticNonzero, hard,
                       PearlJobContextError::InvalidSyntheticContext,
                       "synthetic nonzero source target is refused before override");
    PreparedJobRequest unknown = request;
    unknown.mode = static_cast<PreparedJobMode>(99);
    expectBuildFailure(syntheticJob, unknown,
                       PearlJobContextError::InvalidSyntheticContext,
                       "unknown preparation mode is refused");

    PreparedJobRequest invalidConfig = hard;
    invalidConfig.config.tileH = 1;
    expectBuildFailure(syntheticJob, invalidConfig,
                       PearlJobContextError::InvalidMiningConfig,
                       "hard mode cannot hide invalid mining configuration");
}

void testExactComputationalIdentity() {
    const PreparedJobRequest request = baseRequest();
    const PreparedJobHost prepared = build(baseJob(), request,
                                           "base preparation succeeds");
    check(prepared.network == PearlNetwork::Mainnet &&
              prepared.height == kPearlMainnetV3Activation &&
              prepared.targetSemantics ==
                  PearlTargetSemantics::BaseBeforePenalty &&
              prepared.certificateVersion == PearlCertificateVersion::V3,
          "canonical typed context is retained exactly");

    Job differentTransport = baseJob("pool-job-B");
    PreparedJobHost transportPrepared = build(
        differentTransport, request, "different transport id builds");
    check(differentTransport.extra != baseJob().extra &&
              transportPrepared == prepared,
          "transport-job-id-only change reuses computational identity");
    PreparedJobHost emptyTransport = build(
        baseJob(""), request, "empty public transport id builds");
    check(emptyTransport == prepared &&
              pearl::preparedJobHostConsistent(emptyTransport),
          "canonical public empty transport id reuses and reconstructs");

    PearlJobContext changedHeader;
    setContext(&changedHeader, PearlNetwork::Mainnet,
               kPearlMainnetV3Activation, PearlCertificateVersion::V3);
    for (size_t i = 0; i < changedHeader.header.size(); ++i) {
        PearlJobContext one = changedHeader;
        one.header[i] ^= 1;
        check(build(jobFor(one, baseTarget()), request,
                    "header mutation builds") != prepared,
              "every header byte changes identity");
    }

    PearlJobContext finalContext;
    setContext(&finalContext, PearlNetwork::Mainnet,
               kPearlMainnetV3Activation, PearlCertificateVersion::V3,
               PearlTargetSemantics::FinalAdjustedBound);
    Job finalJob = jobFor(finalContext, baseTarget());
    const PreparedJobHost finalPrepared = build(
        finalJob, request, "final-adjusted mutation baseline builds");
    check(finalPrepared.targetBytesLE[0] == 0xef &&
              finalPrepared.targetBytesLE[1] == 0xcd &&
              finalPrepared.targetBytesLE[7] == 0x01 &&
              finalPrepared.targetBytesLE[8] == 0x77 &&
              finalPrepared.targetBytesLE[24] == 0x03 &&
              finalPrepared.targetBytesLE[31] == 0x00,
          "known target limbs serialize least-significant byte first");
    for (size_t limb = 0; limb < 4; ++limb) {
        for (unsigned byte = 0; byte < 8; ++byte) {
            Job changed = finalJob;
            changed.target[limb] ^= UINT64_C(1) << (byte * 8);
            check(build(changed, request, "target mutation builds") !=
                      finalPrepared,
                  "every source-target byte changes identity");
        }
    }

    struct RequestMutation {
        const char *name;
        PreparedJobRequest request;
    };
    PreparedJobRequest m = request;
    m.m = 8192;
    PreparedJobRequest n = request;
    n.n = 32768;
    PreparedJobRequest k = request;
    k.config.commonDim = 4096;
    PreparedJobRequest rank = request;
    rank.config.commonDim = 4096;
    rank.config.rank = 256;
    PreparedJobRequest tileH = request;
    tileH.config.tileH = 8;
    PreparedJobRequest tileW = request;
    tileW.config.tileW = 8;
    PreparedJobRequest plan = request;
    plan.preparationPlanId ^= 1;
    PreparedJobRequest swapped = request;
    swapped.m = request.n;
    swapped.n = request.m;
    const RequestMutation mutations[] = {
        {"m changes identity", m},       {"n changes identity", n},
        {"k changes identity", k},       {"rank changes identity", rank},
        {"tile H changes identity", tileH},
        {"tile W changes identity", tileW},
        {"plan changes identity", plan},
        {"m/n swap changes identity", swapped},
    };
    for (const RequestMutation &mutation : mutations)
        check(build(baseJob(), mutation.request, "request mutation builds") !=
                  prepared,
              mutation.name);

    PearlJobContext testnetContext;
    setContext(&testnetContext, PearlNetwork::Testnet,
               kPearlMainnetV3Activation, PearlCertificateVersion::V3);
    Job otherNetwork = jobFor(testnetContext, baseTarget());
    check(build(otherNetwork, request, "network mutation builds") != prepared,
          "network changes identity");
    PearlJobContext otherHeightContext;
    setContext(&otherHeightContext, PearlNetwork::Mainnet,
               kPearlMainnetV3Activation + 1,
               PearlCertificateVersion::V3);
    Job otherHeight = jobFor(otherHeightContext, baseTarget());
    check(build(otherHeight, request, "height mutation builds") != prepared,
          "height changes identity");
    check(finalPrepared != prepared, "canonical target semantics is identity");

    for (size_t i = 0; i < prepared.jobKeyBytes.size(); ++i) {
        PreparedJobHost changed = prepared;
        changed.jobKeyBytes[i] ^= 1;
        check(changed != prepared, "every derived job-key byte is identity");
        changed = prepared;
        changed.seedABytes[i] ^= 1;
        check(changed != prepared, "every derived A-seed byte is identity");
        changed = prepared;
        changed.seedBBytes[i] ^= 1;
        check(changed != prepared, "every derived B-seed byte is identity");
        changed = prepared;
        changed.targetBytesLE[i] ^= 1;
        check(changed != prepared, "every derived target byte is identity");
    }
    PreparedJobHost changed = prepared;
    ++changed.schemaVersion;
    check(changed != prepared, "schema version changes identity");
    changed = prepared;
    changed.certificateVersion = PearlCertificateVersion::V2;
    check(changed != prepared, "certificate version changes identity");
    changed = prepared;
    changed.resolvedBound.v[2] ^= 1;
    check(changed != prepared, "resolved target changes identity");
    changed = prepared;
    changed.jobSalt ^= 1;
    check(changed != prepared, "job salt changes identity");
    changed = prepared;
    changed.btSeed ^= 1;
    check(changed != prepared, "B-transpose seed changes identity");
    uint64_t wantSalt = 0;
    for (unsigned i = 0; i < 8; ++i)
        wantSalt |= static_cast<uint64_t>(prepared.jobKeyBytes[i]) << (8 * i);
    check(prepared.jobSalt == wantSalt, "job salt is explicit little-endian");
}

void testLimitsAndAllocationFailure() {
    const Job job = baseJob();
    PreparedJobRequest request = baseRequest();
    check(!pearl::buildPreparedJobHost(job, nullptr, request, nullptr),
          "null output is refused");

    request.config.rank = 16;
    expectBuildFailure(job, request, PearlJobContextError::InvalidMiningConfig,
                       "rank below protocol floor is refused");
    request = baseRequest();
    request.config.commonDim = 2000;
    expectBuildFailure(job, request, PearlJobContextError::InvalidMiningConfig,
                       "unaligned common dimension is refused");
    request = baseRequest();
    request.config.tileH = 15;
    expectBuildFailure(job, request, PearlJobContextError::InvalidMiningConfig,
                       "odd tile side is refused");
    request = baseRequest();
    request.m = (1u << 24) + 1;
    expectBuildFailure(job, request, PearlJobContextError::InvalidMiningConfig,
                       "m resource limit is enforced");
    request = baseRequest();
    request.preparationPlanId = 0;
    expectBuildFailure(job, request, PearlJobContextError::InvalidMiningConfig,
                       "zero preparation plan id is refused");
    request = baseRequest();
    request.preparationPlanId = 1;
    check(build(job, request, "minimum plan id builds").initialized(),
          "smallest nonzero preparation plan id is accepted");
    request = baseRequest();
    request.preparationPlanId = UINT64_MAX;
    check(build(job, request, "maximum plan id builds").initialized(),
          "nonzero preparation plan boundary is accepted");
    request = baseRequest();
    request.n = 8;
    expectBuildFailure(job, request, PearlJobContextError::InvalidMiningConfig,
                       "tile that does not fit n is refused");
    request = baseRequest();
    request.m = 1u << 24;
    request.n = 1u << 24;
    check(build(job, request, "maximum shape builds").initialized(),
          "shape resource boundary is inclusive");
    request = baseRequest();
    request.config.rank = 1024;
    request.config.commonDim = 16384;
    check(build(job, request, "maximum rank builds").initialized(),
          "rank resource boundary is inclusive");
    request = baseRequest();
    request.config.tileH = 2;
    request.config.tileW = 16;
    check(build(job, request, "minimum tile builds").initialized(),
          "minimum tile area is accepted");

    Job overflow = job;
    for (uint64_t &limb : overflow.target) limb = UINT64_MAX;
    expectBuildFailure(overflow, baseRequest(),
                       PearlJobContextError::TargetOverflow,
                       "penalized maximum target overflow is refused");
    PearlJobContext finalContext;
    setContext(&finalContext, PearlNetwork::Mainnet,
               kPearlMainnetV3Activation, PearlCertificateVersion::V3,
               PearlTargetSemantics::FinalAdjustedBound);
    Job maximumFinal = jobFor(finalContext,
                              {UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX});
    PreparedJobHost maximum = build(maximumFinal, baseRequest(),
                                    "final maximum target builds");
    check(maximum.resolvedBound.v[0] == UINT64_MAX &&
              maximum.resolvedBound.v[3] == UINT64_MAX,
          "final-adjusted maximum target is exact");

    const PreparedJobHost sentinel = build(
        job, baseRequest(), "allocation sentinel builds");
    for (uint64_t checkpoint = 0; checkpoint < 4; ++checkpoint) {
        PreparedJobHost out = sentinel;
        std::string error;
        PearlJobContextError code = PearlJobContextError::None;
        pearlSetJobContextAllocationCountdownForTest(checkpoint);
        check(!pearl::buildPreparedJobHost(job, nullptr, baseRequest(), &out,
                                           &error, &code),
              "every canonical context allocation failure is contained");
        pearlDisableJobContextAllocationFailureForTest();
        check(out == PreparedJobHost{} &&
                  code == PearlJobContextError::AllocationFailure,
              "every allocation failure leaves zero prepared output");
    }
    PreparedJobHost afterAllCheckpoints;
    pearlSetJobContextAllocationCountdownForTest(4);
    check(pearl::buildPreparedJobHost(job, nullptr, baseRequest(),
                                      &afterAllCheckpoints),
          "build succeeds after all four canonical allocation checkpoints");
    pearlDisableJobContextAllocationFailureForTest();
    check(afterAllCheckpoints == sentinel,
          "post-checkpoint build publishes the exact prepared state");
}

constexpr size_t kDeviceSlots = 13;

struct FakeDevice {
    std::array<int, kDeviceSlots> generation{};
    std::array<unsigned, kDeviceSlots> writes{};
    unsigned deviceCalls = 0;

    void fill(int value) noexcept { generation.fill(value); }

    bool rewrite(JobPublication *publication, const PreparedJobHost &candidate,
                 int nextGeneration, size_t failBeforeSlot,
                 bool ambiguousCompletion) {
        if (!publication->beginRewrite()) return false;
        for (size_t slot = 0; slot < kDeviceSlots; ++slot) {
            check(publication->state() == JobPublicationState::Dirty,
                  "publication stays dirty during every device write");
            if (slot == failBeforeSlot) {
                publication->abandonRetryable();
                return false;
            }
            generation[slot] = nextGeneration;
            ++writes[slot];
            ++deviceCalls;
        }
        check(publication->state() == JobPublicationState::Dirty,
              "publication is dirty at completion boundary");
        if (failBeforeSlot == kDeviceSlots) {
            publication->abandonRetryable();
            return false;
        }
        if (ambiguousCompletion) {
            publication->poison();
            return false;
        }
        return publication->publish(candidate);
    }
};

void readyWith(JobPublication *publication, const PreparedJobHost &job) {
    check(publication->beginRewrite(), "initial rewrite begins");
    check(publication->publish(job), "initial rewrite publishes");
}

void testPublicationAndDeviceFailures() {
    const PreparedJobHost a = build(baseJob(), baseRequest(), "job A builds");
    Job bJob = baseJob();
    bJob.target[1] ^= UINT64_C(0x100);
    const PreparedJobHost b = build(bJob, baseRequest(), "job B builds");

    JobPublication publication;
    PreparedJobHost observed = a;
    check(publication.state() == JobPublicationState::Empty,
          "publication defaults empty");
    check(!publication.matches(a) && !publication.readReady(&observed),
          "empty publication cannot match or read");
    check(!publication.readReady(nullptr), "null publication read is refused");
    check(observed == PreparedJobHost{}, "empty read returns zero state");
    check(!publication.publish(a), "publish without rewrite is refused");
    check(publication.beginRewrite(), "inconsistent rewrite begins");
    PreparedJobHost inconsistent = a;
    inconsistent.jobKeyBytes[0] ^= 1;
    check(!publication.publish(inconsistent),
          "hand-assembled inconsistent candidate is refused");
    check(publication.state() == JobPublicationState::Dirty,
          "refused inconsistent publish remains fail-closed dirty");
    publication.resetAfterDeviceRecreate();
    readyWith(&publication, a);
    check(publication.beginRewrite(), "allocation-failure rewrite begins");
    pearlSetJobContextAllocationCountdownForTest(0);
    check(!publication.publish(b),
          "consistency reconstruction allocation failure is refused");
    pearlDisableJobContextAllocationFailureForTest();
    check(publication.state() == JobPublicationState::Dirty &&
              !publication.matches(a) && !publication.matches(b),
          "publication allocation failure exposes no stale ready state");
    publication.resetAfterDeviceRecreate();
    readyWith(&publication, a);
    check(publication.matches(a) && !publication.matches(b),
          "ready publication matches exact job only");

    Job invalid = baseJob();
    invalid.msg[0] ^= 1;
    PreparedJobHost failed;
    check(!pearl::buildPreparedJobHost(invalid, nullptr, baseRequest(), &failed),
          "host validation fails before device mutation");
    check(publication.matches(a),
          "pre-device validation failure retains ready A");

    for (size_t failpoint = 0; failpoint <= kDeviceSlots; ++failpoint) {
        publication.resetAfterDeviceRecreate();
        readyWith(&publication, a);
        FakeDevice device;
        device.fill(1);
        check(!device.rewrite(&publication, b, 2, failpoint, false),
              "injected device failure is reported");
        check(publication.state() == JobPublicationState::Dirty &&
                  !publication.matches(a) && !publication.matches(b),
              "partial generation cannot cache-hit");
        observed = a;
        check(!publication.readReady(&observed) &&
                  observed == PreparedJobHost{},
              "partial generation reads as zero");
        const auto beforeRetry = device.writes;
        check(device.rewrite(&publication, b, 2, SIZE_MAX, false),
              "complete retry succeeds");
        bool allRewritten = true;
        for (size_t slot = 0; slot < kDeviceSlots; ++slot)
            if (device.writes[slot] != beforeRetry[slot] + 1 ||
                device.generation[slot] != 2)
                allRewritten = false;
        check(allRewritten, "retry rewrites every device fixture");
        check(publication.matches(b), "retry publishes only completed B");
    }

    publication.resetAfterDeviceRecreate();
    readyWith(&publication, a);
    FakeDevice cacheHit;
    if (!publication.matches(a))
        (void)cacheHit.rewrite(&publication, a, 1, SIZE_MAX, false);
    check(cacheHit.deviceCalls == 0, "exact cache hit performs no device work");

    check(publication.beginRewrite(), "throwing rewrite begins dirty");
    try {
        throw std::runtime_error("injected callback failure");
    } catch (const std::runtime_error &) {
        publication.abandonRetryable();
    }
    check(publication.state() == JobPublicationState::Dirty &&
              !publication.matches(a),
          "exception cannot publish stale state");

    publication.resetAfterDeviceRecreate();
    readyWith(&publication, a);
    FakeDevice ambiguous;
    ambiguous.fill(1);
    check(!ambiguous.rewrite(&publication, b, 2, SIZE_MAX, true),
          "ambiguous completion is reported");
    check(publication.poisoned() && !publication.beginRewrite(),
          "poisoned state refuses same-job retry");
    publication.resetAfterDeviceRecreate();
    check(ambiguous.rewrite(&publication, b, 2, SIZE_MAX, false) &&
              publication.matches(b),
          "device recreation permits full rebuild");
}

}  // namespace

int main() {
    testCanonicalForkBoundaries();
    testCanonicalValidationAndSyntheticMode();
    testExactComputationalIdentity();
    testLimitsAndAllocationFailure();
    testPublicationAndDeviceFailures();

    if (failures == 0) {
        std::printf("Pearl canonical host job publication: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "Pearl canonical host job publication: %d failure(s)\n",
                 failures);
    return 1;
}
