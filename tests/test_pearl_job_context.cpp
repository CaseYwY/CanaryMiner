#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <limits>
#include <string>

#define PEARL_JOB_CONTEXT_ENABLE_TEST_SEAMS 1
#include "../src/core/pearl_job_context.h"

using namespace om;

namespace {

int failures = 0;

void check(const char *name, bool condition) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        failures++;
    }
}

PearlJobContext contextFor(PearlNetwork network, uint64_t height,
                           PearlCertificateVersion certificate,
                           PearlTargetSemantics semantics =
                               PearlTargetSemantics::BaseBeforePenalty) {
    PearlJobContext context{network, semantics, height, certificate, {},
                            "pool-job"};
    for (size_t i = 0; i < context.header.size(); ++i)
        context.header[i] = static_cast<uint8_t>(i * 37u + 11u);
    return context;
}

Job jobFor(const PearlJobContext &context, uint64_t target = 5) {
    Job job{{}, {}, context.height, std::string("pending"), std::string(),
            false};
    std::string error;
    PearlJobContextError code = PearlJobContextError::None;
    check("test context encodes",
          encodePearlJobContext(context, &job.extra, &error, &code));
    pearl::b3::hash(nullptr, context.header.data(), context.header.size(),
                    job.msg);
    job.epoch = context.height;
    job.jobId = context.transportJobId;
    job.target[0] = target;
    job.valid = true;
    return job;
}

bool replaceOnce(std::string *text, const std::string &from,
                 const std::string &to) {
    const size_t at = text->find(from);
    if (at == std::string::npos) return false;
    text->replace(at, from.size(), to);
    return true;
}

bool decodeFails(const Job &job, PearlJobContextError expected) {
    PearlJobContext output;
    std::string error;
    PearlJobContextError code = PearlJobContextError::None;
    return !decodePearlJobContext(job, &output, &error, &code) &&
           code == expected && !error.empty();
}

bool validateFails(const Job &job, const PearlSourcePolicy *policy,
                   PearlJobContextError expected) {
    PearlJobContext output;
    std::string error;
    PearlJobContextError code = PearlJobContextError::None;
    return !validatePearlJobContext(job, policy, &output, &error, &code) &&
           code == expected && !error.empty();
}

bool sameU256(const pearl::U256 &a, const pearl::U256 &b) {
    return memcmp(a.v, b.v, sizeof(a.v)) == 0;
}

void syntheticBuilderTest() {
    static_assert(noexcept(buildPearlSyntheticBenchmarkJob(nullptr)),
                  "synthetic builder must contain allocation failures");
    const auto empty = [](const Job &job) {
        uint8_t zero[32]{};
        return !job.valid && job.epoch == 0 && job.extra.empty() &&
               job.jobId.empty() && pearlTargetIsZero(job.target) &&
               memcmp(job.msg, zero, sizeof(zero)) == 0;
    };
    const Job old = jobFor(contextFor(PearlNetwork::Mainnet,
                                     kPearlMainnetV3Activation,
                                     PearlCertificateVersion::V3));
    Job job = old;
    std::string error = "old error";
    PearlJobContextError code = PearlJobContextError::InvalidJob;
    check("actual synthetic builder succeeds over a populated output",
          buildPearlSyntheticBenchmarkJob(&job, &error, &code) && job.valid &&
              error.empty() && code == PearlJobContextError::None);
    const PearlSourcePolicy policy{PearlNetwork::SyntheticBenchmark,
                                   PearlTargetSemantics::FinalAdjustedBound};
    PearlJobContext context;
    check("built synthetic job validates and has exact canonical fields",
          validatePearlJobContext(job, &policy, &context, &error, &code) &&
              context.network == PearlNetwork::SyntheticBenchmark &&
              context.targetSemantics == PearlTargetSemantics::FinalAdjustedBound &&
              context.certificateVersion == PearlCertificateVersion::V1 &&
              context.height == kPearlBenchmarkEpoch &&
              context.transportJobId.empty() && job.jobId.empty() &&
              pearlTargetIsZero(job.target) && pearlSyntheticHeader(context.header));
    std::string encoded;
    check("built synthetic envelope roundtrips exactly",
          encodePearlJobContext(context, &encoded, &error, &code) &&
              encoded == job.extra);
    uint8_t header[kPearlBenchmarkHeaderBytes], digest[32];
    fillPearlBenchmarkHeader(header);
    pearl::b3::hash(nullptr, header, sizeof(header), digest);
    check("built synthetic message is the pinned header digest",
          memcmp(job.msg, digest, sizeof(digest)) == 0);
    Job repeated{};
    check("default-output arguments build a deterministic fixture",
          buildPearlSyntheticBenchmarkJob(&repeated) &&
              repeated.extra == job.extra && repeated.epoch == job.epoch &&
              memcmp(repeated.msg, job.msg, sizeof(job.msg)) == 0);
    job.target[0] = 1;
    check("builder does not relax the nonzero synthetic-target prohibition",
          validateFails(job, &policy,
                        PearlJobContextError::InvalidSyntheticContext));
    check("null synthetic output fails with diagnostic",
          !buildPearlSyntheticBenchmarkJob(nullptr, &error, &code) &&
              code == PearlJobContextError::NullOutput && !error.empty());
    check("null synthetic output with no diagnostics fails",
          !buildPearlSyntheticBenchmarkJob(nullptr));
    for (unsigned checkpoint = 0; checkpoint < 3; ++checkpoint) {
        job = old;
        pearlSetJobContextAllocationCountdownForTest(checkpoint);
        check("each synthetic allocation failure resets old job authority",
              !buildPearlSyntheticBenchmarkJob(&job, &error, &code) &&
                  code == PearlJobContextError::AllocationFailure &&
                  !error.empty() && empty(job));
        pearlDisableJobContextAllocationFailureForTest();
        check("synthetic fixture recovers after allocation failure",
              buildPearlSyntheticBenchmarkJob(&job, &error, &code) && job.valid);
    }
    for (unsigned field = 0; field < 2; ++field) {
        job = old;
        std::string *alias = field == 0 ? &job.extra : &job.jobId;
        check("synthetic diagnostic alias refuses and resets output",
              !buildPearlSyntheticBenchmarkJob(&job, alias, &code) &&
                  code == PearlJobContextError::AliasedOutput && empty(job));
    }
}

}  // namespace

int main() {
    syntheticBuilderTest();
    static_assert(noexcept(encodePearlJobContext(
                      std::declval<const PearlJobContext &>(),
                      static_cast<std::string *>(nullptr),
                      static_cast<std::string *>(nullptr))),
                  "context encoding must contain allocation failures");
    static_assert(noexcept(decodePearlJobContext(
                      std::declval<const Job &>(),
                      static_cast<PearlJobContext *>(nullptr),
                      static_cast<std::string *>(nullptr))),
                  "context decoding must contain allocation failures");

    check("mainnet V2 activation is pinned",
          pearlV2Activation(PearlNetwork::Mainnet) == 71935);
    check("testnet V2 activation is pinned",
          pearlV2Activation(PearlNetwork::Testnet) == 1);
    check("testnet2 V2 activation is pinned",
          pearlV2Activation(PearlNetwork::Testnet2) == 54869);
    check("mainnet rank-penalty activation is pinned",
          pearlRankPenaltyActivation(PearlNetwork::Mainnet) == 96251);
    check("testnet rank-penalty activation is pinned",
          pearlRankPenaltyActivation(PearlNetwork::Testnet) == 36761);
    check("testnet2 rank-penalty activation is pinned",
          pearlRankPenaltyActivation(PearlNetwork::Testnet2) == 80627);
    check("regtest rank-penalty activation is pinned",
          pearlRankPenaltyActivation(PearlNetwork::Regtest) == 1);
    check("mainnet V3 activation is pinned",
          pearlV3Activation(PearlNetwork::Mainnet) == 99000);
    check("testnet V3 activation is pinned",
          pearlV3Activation(PearlNetwork::Testnet) == 38648);
    check("testnet2 V3 activation is pinned",
          pearlV3Activation(PearlNetwork::Testnet2) == 83109);
    check("regtest V3 activation is pinned",
          pearlV3Activation(PearlNetwork::Regtest) == 1);
    check("synthetic benchmark has no public activation",
          pearlV3Activation(PearlNetwork::SyntheticBenchmark) ==
              std::numeric_limits<uint64_t>::max());
    check("unknown network has no inferred activation",
          pearlV3Activation(static_cast<PearlNetwork>(255)) ==
              std::numeric_limits<uint64_t>::max());

    struct Boundary {
        PearlNetwork network;
        uint64_t height;
        PearlCertificateVersion certificate;
        bool allowed;
    };
    static constexpr Boundary boundaries[] = {
        {PearlNetwork::Mainnet, 71934, PearlCertificateVersion::V1, false},
        {PearlNetwork::Mainnet, 71934, PearlCertificateVersion::V2, false},
        {PearlNetwork::Mainnet, 71935, PearlCertificateVersion::V2, true},
        {PearlNetwork::Mainnet, 71935, PearlCertificateVersion::V1, false},
        {PearlNetwork::Mainnet, 98999, PearlCertificateVersion::V2, true},
        {PearlNetwork::Mainnet, 98999, PearlCertificateVersion::V3, false},
        {PearlNetwork::Mainnet, 99000, PearlCertificateVersion::V3, true},
        {PearlNetwork::Mainnet, 99000, PearlCertificateVersion::V2, false},
        {PearlNetwork::Mainnet, 99001, PearlCertificateVersion::V3, true},
        {PearlNetwork::Testnet, 0, PearlCertificateVersion::V1, false},
        {PearlNetwork::Testnet, 0, PearlCertificateVersion::V2, false},
        {PearlNetwork::Testnet, 1, PearlCertificateVersion::V2, true},
        {PearlNetwork::Testnet, 1, PearlCertificateVersion::V1, false},
        {PearlNetwork::Testnet, 38647, PearlCertificateVersion::V2, true},
        {PearlNetwork::Testnet, 38647, PearlCertificateVersion::V3, false},
        {PearlNetwork::Testnet, 38648, PearlCertificateVersion::V3, true},
        {PearlNetwork::Testnet, 38648, PearlCertificateVersion::V2, false},
        {PearlNetwork::Testnet, 38649, PearlCertificateVersion::V3, true},
        {PearlNetwork::Testnet2, 54868, PearlCertificateVersion::V1, false},
        {PearlNetwork::Testnet2, 54868, PearlCertificateVersion::V2, false},
        {PearlNetwork::Testnet2, 54869, PearlCertificateVersion::V2, true},
        {PearlNetwork::Testnet2, 54869, PearlCertificateVersion::V1, false},
        {PearlNetwork::Testnet2, 83108, PearlCertificateVersion::V2, true},
        {PearlNetwork::Testnet2, 83108, PearlCertificateVersion::V3, false},
        {PearlNetwork::Testnet2, 83109, PearlCertificateVersion::V3, true},
        {PearlNetwork::Testnet2, 83109, PearlCertificateVersion::V2, false},
        {PearlNetwork::Testnet2, 83110, PearlCertificateVersion::V3, true},
    };
    for (const Boundary &boundary : boundaries)
        check("public supported-floor/fork boundary selects exact certificate",
              pearlCertificateAllowed(boundary.network, boundary.height,
                                      boundary.certificate) == boundary.allowed);
    check("V1 is never inferred on public networks",
          !pearlCertificateAllowed(PearlNetwork::Mainnet, 0,
                                   PearlCertificateVersion::V1));
    check("future certificate is rejected on public networks",
          !pearlCertificateAllowed(
              PearlNetwork::Mainnet, 99000,
              static_cast<PearlCertificateVersion>(4)));
    check("unknown network cannot borrow the mainnet schedule",
          !pearlCertificateAllowed(
              static_cast<PearlNetwork>(255), 99000,
              PearlCertificateVersion::V3));

    struct SupportedFloor {
        PearlNetwork network;
        uint64_t floor;
    };
    static constexpr SupportedFloor supportedFloors[] = {
        {PearlNetwork::Mainnet, 71935},
        {PearlNetwork::Testnet, 1},
        {PearlNetwork::Testnet2, 54869},
    };
    for (const SupportedFloor &floor : supportedFloors) {
        PearlJobContext below = contextFor(
            floor.network, floor.floor - 1, PearlCertificateVersion::V2);
        std::string rejected = "must-clear";
        std::string floorError;
        PearlJobContextError floorCode = PearlJobContextError::None;
        check("context encoder fails closed below supported public floor",
              !encodePearlJobContext(below, &rejected, &floorError,
                                     &floorCode) &&
                  floorCode == PearlJobContextError::CertificateNotAllowed &&
                  rejected.empty() && !floorError.empty());
    }

    PearlJobContext canonical = contextFor(
        PearlNetwork::Mainnet, 99000, PearlCertificateVersion::V3);
    canonical.transportJobId = "job:quote\"/slash\nline";
    Job canonicalJob = jobFor(canonical);
    PearlJobContext decoded;
    std::string error = "stale error";
    PearlJobContextError code = PearlJobContextError::ContextMismatch;
    check("canonical envelope validates",
          validatePearlJobContext(canonicalJob, nullptr, &decoded, &error,
                                  &code) &&
              code == PearlJobContextError::None && error.empty());
    check("opaque job id punctuation and newline round-trip exactly",
          pearlJobContextEqual(canonical, decoded));

    PearlJobContext binaryId = canonical;
    static const char opaqueWithNul[] = "job\0binary:tail\n";
    binaryId.transportJobId.assign(opaqueWithNul, sizeof(opaqueWithNul) - 1);
    Job binaryIdJob = jobFor(binaryId);
    check("embedded-NUL opaque job id validates",
          validatePearlJobContext(binaryIdJob, nullptr, &decoded, &error,
                                  &code));
    check("embedded-NUL opaque job id round-trips byte-exactly",
          pearlJobContextEqual(binaryId, decoded));
    check("canonical envelope revalidates after opaque-ID fixture",
          validatePearlJobContext(canonicalJob, nullptr, &decoded, &error,
                                  &code));
    std::string reencoded;
    check("decoded context re-encodes",
          encodePearlJobContext(decoded, &reencoded, &error, &code));
    check("decode/encode is byte-identical", reencoded == canonicalJob.extra);
    check("canonical tokens and lowercase header are present",
          canonicalJob.extra.find(
              "pearl-job-v1:mainnet:base-before-penalty:v3:") == 0);

    PearlJobContext mismatchSentinel = contextFor(
        PearlNetwork::Testnet2, 83109, PearlCertificateVersion::V3);
    const PearlJobContext mismatchSentinelBefore = mismatchSentinel;
    Job genericIdEmpty = canonicalJob;
    genericIdEmpty.jobId.clear();
    error = "stale error";
    code = PearlJobContextError::None;
    check("populated envelope ID cannot validate with empty Job::jobId",
          !validatePearlJobContext(genericIdEmpty, nullptr, &mismatchSentinel,
                                   &error, &code) &&
              code == PearlJobContextError::ContextMismatch &&
              !error.empty() &&
              pearlJobContextEqual(mismatchSentinel,
                                   mismatchSentinelBefore));

    mismatchSentinel = mismatchSentinelBefore;
    code = PearlJobContextError::None;
    check("valid job rejects error aliasing context output before mutation",
          !validatePearlJobContext(
              canonicalJob, nullptr, &mismatchSentinel,
              &mismatchSentinel.transportJobId, &code) &&
              code == PearlJobContextError::AliasedOutput &&
              pearlJobContextEqual(mismatchSentinel,
                                   mismatchSentinelBefore));
    mismatchSentinel = mismatchSentinelBefore;
    code = PearlJobContextError::None;
    check("mismatched job rejects error aliasing context before mutation",
          !validatePearlJobContext(
              genericIdEmpty, nullptr, &mismatchSentinel,
              &mismatchSentinel.transportJobId, &code) &&
              code == PearlJobContextError::AliasedOutput &&
              pearlJobContextEqual(mismatchSentinel,
                                   mismatchSentinelBefore));

    PearlJobContext decodeAliasOutput = mismatchSentinelBefore;
    code = PearlJobContextError::None;
    check("decoder rejects error aliasing context output before mutation",
          !decodePearlJobContext(
              canonicalJob, &decodeAliasOutput,
              &decodeAliasOutput.transportJobId, &code) &&
              code == PearlJobContextError::AliasedOutput &&
              pearlJobContextEqual(decodeAliasOutput,
                                   mismatchSentinelBefore));

    Job extraAliasJob = canonicalJob;
    const std::string extraAliasBefore = extraAliasJob.extra;
    const std::string extraAliasIdBefore = extraAliasJob.jobId;
    PearlJobContext extraAliasOutput = mismatchSentinelBefore;
    code = PearlJobContextError::None;
    check("decoder rejects error aliasing nominally const Job::extra",
          !decodePearlJobContext(extraAliasJob, &extraAliasOutput,
                                 &extraAliasJob.extra, &code) &&
              code == PearlJobContextError::AliasedOutput &&
              extraAliasJob.extra == extraAliasBefore &&
              extraAliasJob.jobId == extraAliasIdBefore &&
              pearlJobContextEqual(extraAliasOutput,
                                   mismatchSentinelBefore));

    Job idAliasJob = canonicalJob;
    idAliasJob.valid = false;
    const std::string idAliasExtraBefore = idAliasJob.extra;
    const std::string idAliasBefore = idAliasJob.jobId;
    PearlJobContext idAliasOutput = mismatchSentinelBefore;
    code = PearlJobContextError::None;
    check("validator rejects Job::jobId error alias before invalid-job error",
          !validatePearlJobContext(idAliasJob, nullptr, &idAliasOutput,
                                   &idAliasJob.jobId, &code) &&
              code == PearlJobContextError::AliasedOutput &&
              idAliasJob.extra == idAliasExtraBefore &&
              idAliasJob.jobId == idAliasBefore &&
              pearlJobContextEqual(idAliasOutput,
                                   mismatchSentinelBefore));

    Job invalidExtraAliasJob = canonicalJob;
    invalidExtraAliasJob.valid = false;
    const std::string invalidExtraAliasBefore = invalidExtraAliasJob.extra;
    const std::string invalidExtraAliasIdBefore = invalidExtraAliasJob.jobId;
    PearlJobContext invalidExtraAliasOutput = mismatchSentinelBefore;
    code = PearlJobContextError::None;
    check("validator rejects Job::extra error alias before invalid-job error",
          !validatePearlJobContext(invalidExtraAliasJob, nullptr,
                                   &invalidExtraAliasOutput,
                                   &invalidExtraAliasJob.extra, &code) &&
              code == PearlJobContextError::AliasedOutput &&
              invalidExtraAliasJob.extra == invalidExtraAliasBefore &&
              invalidExtraAliasJob.jobId == invalidExtraAliasIdBefore &&
              pearlJobContextEqual(invalidExtraAliasOutput,
                                   mismatchSentinelBefore));

    PearlJobContext publicEmpty = canonical;
    publicEmpty.transportJobId.clear();
    Job publicEmptyJob = jobFor(publicEmpty);
    error = "stale error";
    code = PearlJobContextError::ContextMismatch;
    check("public context preserves both-empty transport-ID compatibility",
          validatePearlJobContext(publicEmptyJob, nullptr, &decoded, &error,
                                  &code) &&
              decoded.transportJobId.empty() && publicEmptyJob.jobId.empty() &&
              code == PearlJobContextError::None && error.empty());
    Job envelopeIdEmpty = publicEmptyJob;
    envelopeIdEmpty.jobId = canonical.transportJobId;
    mismatchSentinel = mismatchSentinelBefore;
    error = "stale error";
    code = PearlJobContextError::None;
    check("empty envelope ID cannot validate with populated Job::jobId",
          !validatePearlJobContext(envelopeIdEmpty, nullptr,
                                   &mismatchSentinel, &error, &code) &&
              code == PearlJobContextError::ContextMismatch &&
              !error.empty() &&
              pearlJobContextEqual(mismatchSentinel,
                                   mismatchSentinelBefore));

    static const char *badCertificates[] = {
        "3", "3junk", "+3", "03", "\"3\"", "3.0", "v0", "v4", "v255"};
    for (const char *token : badCertificates) {
        Job bad = canonicalJob;
        std::string replacement(":");
        replacement.append(token);
        replacement.push_back(':');
        check("test replaces certificate token",
              replaceOnce(&bad.extra, ":v3:", replacement));
        check("malformed or future certificate token is rejected",
              decodeFails(bad,
                          PearlJobContextError::InvalidCertificateVersion));
    }
    Job bad = canonicalJob;
    check("test replaces network token",
          replaceOnce(&bad.extra, ":mainnet:", ":future-net:"));
    check("unknown network token fails",
          decodeFails(bad, PearlJobContextError::InvalidNetwork));
    bad = canonicalJob;
    check("test replaces target semantics",
          replaceOnce(&bad.extra, ":base-before-penalty:", ":twice:"));
    check("unknown target semantics fails",
          decodeFails(bad, PearlJobContextError::InvalidTargetSemantics));
    bad = canonicalJob;
    check("test replaces envelope version",
          replaceOnce(&bad.extra, "pearl-job-v1:", "pearl-job-v2:"));
    check("future envelope version fails",
          decodeFails(bad, PearlJobContextError::MalformedEnvelope));
    bad = canonicalJob;
    bad.extra = bad.extra.substr(bad.extra.find(":v3:") + 1);
    check("legacy header/certificate envelope fails",
          decodeFails(bad, PearlJobContextError::MalformedEnvelope));
    bad = canonicalJob;
    bad.extra.pop_back();
    bad.extra.erase(bad.extra.find(':', bad.extra.find(":v3:") + 4));
    check("missing header separator fails",
          decodeFails(bad, PearlJobContextError::MalformedEnvelope));

    bad = canonicalJob;
    const size_t headerAt = bad.extra.find(":v3:") + 4;
    bad.extra[headerAt] = 'A';
    check("uppercase header hex is non-canonical and rejected",
          decodeFails(bad, PearlJobContextError::InvalidHeader));
    bad = canonicalJob;
    bad.extra[headerAt] = 'g';
    check("non-hex header is rejected",
          decodeFails(bad, PearlJobContextError::InvalidHeader));
    bad = canonicalJob;
    bad.extra.erase(headerAt, 1);
    check("short header is rejected",
          decodeFails(bad, PearlJobContextError::InvalidHeader));

    bad = canonicalJob;
    bad.msg[0] ^= 1;
    check("header/message mismatch fails validation",
          validateFails(bad, nullptr,
                        PearlJobContextError::HeaderMessageMismatch));
    bad = canonicalJob;
    bad.extra[headerAt] = bad.extra[headerAt] == '0' ? '1' : '0';
    check("canonical-looking header mutation still fails message binding",
          validateFails(bad, nullptr,
                        PearlJobContextError::HeaderMessageMismatch));
    bad = canonicalJob;
    bad.valid = false;
    check("invalid generic Job cannot become Pearl work",
          validateFails(bad, nullptr, PearlJobContextError::InvalidJob));
    bad = canonicalJob;
    memset(bad.target, 0, sizeof(bad.target));
    check("zero target is rejected on public networks",
          validateFails(bad, nullptr, PearlJobContextError::ZeroTarget));

    const PearlSourcePolicy exactPolicy{PearlNetwork::Mainnet,
                                         PearlTargetSemantics::BaseBeforePenalty};
    check("matching independent source policy validates",
          validatePearlJobContext(canonicalJob, &exactPolicy, &decoded, &error,
                                  &code));
    const PearlSourcePolicy wrongNetwork{PearlNetwork::Testnet,
                                          PearlTargetSemantics::BaseBeforePenalty};
    check("network policy mismatch fails",
          validateFails(canonicalJob, &wrongNetwork,
                        PearlJobContextError::PolicyMismatch));
    const PearlSourcePolicy wrongSemantics{
        PearlNetwork::Mainnet, PearlTargetSemantics::FinalAdjustedBound};
    check("target-semantics policy mismatch fails",
          validateFails(canonicalJob, &wrongSemantics,
                        PearlJobContextError::PolicyMismatch));
    const PearlSourcePolicy invalidPolicy{
        static_cast<PearlNetwork>(0),
        PearlTargetSemantics::BaseBeforePenalty};
    check("incomplete default-like policy fails closed",
          validateFails(canonicalJob, &invalidPolicy,
                        PearlJobContextError::PolicyMismatch));

    bad = canonicalJob;
    bad.epoch = 98999;
    check("authoritative epoch mutation across activation invalidates cert",
          decodeFails(bad, PearlJobContextError::CertificateNotAllowed));

    Job heightOnly = canonicalJob;
    heightOnly.epoch = canonicalJob.epoch + 1;
    PearlJobContext heightOnlyDecoded;
    check("same envelope at another height decodes from authoritative epoch",
          decodePearlJobContext(heightOnly, &heightOnlyDecoded, &error, &code));
    check("height-only identity is the Job epoch plus unchanged extra",
          heightOnly.extra == canonicalJob.extra &&
              heightOnlyDecoded.height == canonical.height + 1 &&
              !pearlJobContextEqual(heightOnlyDecoded, canonical));
    bad = canonicalJob;
    check("test changes certificate at activation",
          replaceOnce(&bad.extra, ":v3:", ":v2:"));
    check("wrong certificate at activation fails",
          decodeFails(bad, PearlJobContextError::CertificateNotAllowed));

    PearlJobContext mutated = canonical;
    mutated.network = PearlNetwork::Testnet;
    std::string mutatedExtra;
    check("network mutation remains encodable under its schedule",
          encodePearlJobContext(mutated, &mutatedExtra, &error, &code));
    check("network mutation changes canonical identity",
          mutatedExtra != canonicalJob.extra);
    mutated = canonical;
    mutated.targetSemantics = PearlTargetSemantics::FinalAdjustedBound;
    check("semantics mutation encodes",
          encodePearlJobContext(mutated, &mutatedExtra, &error, &code));
    check("semantics mutation changes canonical identity",
          mutatedExtra != canonicalJob.extra);
    mutated = canonical;
    mutated.certificateVersion = PearlCertificateVersion::V2;
    mutated.height = 98999;
    check("height/certificate mutation encodes below activation",
          encodePearlJobContext(mutated, &mutatedExtra, &error, &code));
    check("certificate mutation changes canonical envelope",
          mutatedExtra != canonicalJob.extra);
    mutated = canonical;
    mutated.header[0] ^= 1;
    check("header mutation encodes",
          encodePearlJobContext(mutated, &mutatedExtra, &error, &code));
    check("header mutation changes canonical identity",
          mutatedExtra != canonicalJob.extra);
    mutated = canonical;
    mutated.transportJobId += ":replacement";
    check("job-id mutation encodes",
          encodePearlJobContext(mutated, &mutatedExtra, &error, &code));
    check("job-id mutation changes canonical identity",
          mutatedExtra != canonicalJob.extra);

    PearlJobContext regtest = contextFor(
        PearlNetwork::Regtest, 1, PearlCertificateVersion::V3,
        PearlTargetSemantics::FinalAdjustedBound);
    regtest.transportJobId.clear();
    Job regtestJob = jobFor(regtest);
    check("regtest V3 policy validates",
          validatePearlJobContext(regtestJob, nullptr, &decoded, &error, &code));
    PearlJobContext regtestZero = regtest;
    regtestZero.height = 0;
    std::string refused = "must-stay-until-validated";
    check("regtest height zero cannot borrow the height-one V3 policy",
          !encodePearlJobContext(regtestZero, &refused, &error, &code) &&
              code == PearlJobContextError::CertificateNotAllowed &&
              refused.empty());
    regtest.certificateVersion = PearlCertificateVersion::V2;
    refused = "must-clear";
    check("regtest refuses V2 without borrowing a public schedule",
          !encodePearlJobContext(regtest, &refused, &error, &code) &&
              code == PearlJobContextError::CertificateNotAllowed &&
              refused.empty());

    PearlJobContext synthetic;
    synthetic.network = PearlNetwork::SyntheticBenchmark;
    synthetic.targetSemantics = PearlTargetSemantics::FinalAdjustedBound;
    synthetic.height = kPearlBenchmarkEpoch;
    synthetic.certificateVersion = PearlCertificateVersion::V1;
    uint8_t syntheticHeader[kPearlBenchmarkHeaderBytes];
    fillPearlBenchmarkHeader(syntheticHeader);
    memcpy(synthetic.header.data(), syntheticHeader, synthetic.header.size());
    Job syntheticJob = jobFor(synthetic, 0);
    check("exact synthetic V1 zero-target fixture validates with both IDs empty",
          validatePearlJobContext(syntheticJob, nullptr, &decoded, &error,
                                  &code) &&
              decoded.transportJobId.empty() && syntheticJob.jobId.empty());
    synthetic.height++;
    check("synthetic height is exact",
          !encodePearlJobContext(synthetic, &refused, &error, &code) &&
              code == PearlJobContextError::InvalidSyntheticContext);
    synthetic.height--;
    synthetic.header[0] ^= 1;
    check("synthetic header is exact",
          !encodePearlJobContext(synthetic, &refused, &error, &code) &&
              code == PearlJobContextError::InvalidSyntheticContext);
    bad = syntheticJob;
    bad.target[0] = 1;
    check("synthetic benchmark rejects a submittable nonzero target",
          validateFails(bad, nullptr,
                        PearlJobContextError::InvalidSyntheticContext));

    pearl::MiningConfig config;
    pearl::U256 resolved;
    check("base target resolves through exactly one rank penalty",
          resolvePearlTarget(canonicalJob, canonical, config, &resolved, &error,
                             &code));
    pearl::U256 source = pearl::U256::fromLimbs(canonicalJob.target);
    pearl::U256 once;
    check("reference one-time rank penalty succeeds",
          config.penalizedTarget(source, &once));
    check("base target equals the one-time reference penalty",
          sameU256(resolved, once));
    check("target resolution leaves the source target unchanged",
          canonicalJob.target[0] == 5 && canonicalJob.target[1] == 0 &&
              canonicalJob.target[2] == 0 && canonicalJob.target[3] == 0);

    struct PenaltyBoundary {
        PearlNetwork network;
        uint64_t activation;
    };
    static constexpr PenaltyBoundary penaltyBoundaries[] = {
        {PearlNetwork::Mainnet, 96251},
        {PearlNetwork::Testnet, 36761},
        {PearlNetwork::Testnet2, 80627},
    };
    pearl::MiningConfig invalidPreForkConfig = config;
    invalidPreForkConfig.rank = 0;
    for (const PenaltyBoundary &boundary : penaltyBoundaries) {
        PearlJobContext before = contextFor(
            boundary.network, boundary.activation - 1,
            PearlCertificateVersion::V2);
        Job beforeJob = jobFor(before);
        check("base target is copied verbatim before rank-penalty fork",
              resolvePearlTarget(beforeJob, before, invalidPreForkConfig,
                                 &resolved, &error, &code) &&
                  sameU256(resolved,
                           pearl::U256::fromLimbs(beforeJob.target)));

        PearlJobContext at = contextFor(boundary.network, boundary.activation,
                                        PearlCertificateVersion::V2);
        Job atJob = jobFor(at);
        pearl::U256 atResolved;
        check("base target applies exactly one penalty at rank-penalty fork",
              resolvePearlTarget(atJob, at, config, &atResolved, &error,
                                 &code) &&
                  sameU256(atResolved, once));
        check("active rank penalty requires a valid mining config",
              !resolvePearlTarget(atJob, at, invalidPreForkConfig, &resolved,
                                  &error, &code) &&
                  code == PearlJobContextError::InvalidMiningConfig);
    }
    PearlJobContext regtestBase = contextFor(
        PearlNetwork::Regtest, 1, PearlCertificateVersion::V3);
    regtestBase.transportJobId.clear();
    Job regtestBaseJob = jobFor(regtestBase);
    check("regtest rank penalty is active from height one",
          resolvePearlTarget(regtestBaseJob, regtestBase, config, &resolved,
                             &error, &code) &&
              sameU256(resolved, once));
    check("regtest height-one penalty validates mining config",
          !resolvePearlTarget(regtestBaseJob, regtestBase,
                              invalidPreForkConfig, &resolved, &error,
                              &code) &&
              code == PearlJobContextError::InvalidMiningConfig);

    PearlJobContext finalContext = canonical;
    finalContext.targetSemantics = PearlTargetSemantics::FinalAdjustedBound;
    Job finalJob = jobFor(finalContext);
    pearl::U256 finalResolved;
    check("final adjusted target resolves",
          resolvePearlTarget(finalJob, finalContext, config, &finalResolved,
                             &error, &code));
    check("final adjusted target is copied verbatim",
          sameU256(finalResolved, pearl::U256::fromLimbs(finalJob.target)));
    check("same numeric target under opposite semantics resolves differently",
          !sameU256(once, finalResolved));
    pearl::U256 twice;
    check("a second penalty is numerically distinguishable",
          config.penalizedTarget(once, &twice) && !sameU256(once, twice));
    check("final semantics did not apply that second penalty",
          !sameU256(finalResolved, twice));

    PearlJobContext wrongContext = canonical;
    wrongContext.height++;
    check("mutated caller context cannot resolve a job target",
          !resolvePearlTarget(canonicalJob, wrongContext, config, &resolved,
                              &error, &code) &&
              code == PearlJobContextError::ContextMismatch);
    wrongContext = canonical;
    wrongContext.targetSemantics = PearlTargetSemantics::FinalAdjustedBound;
    check("semantics-only caller-context mutation cannot reuse target state",
          !resolvePearlTarget(canonicalJob, wrongContext, config, &resolved,
                              &error, &code) &&
              code == PearlJobContextError::ContextMismatch);
    pearl::MiningConfig badConfig = config;
    badConfig.rank = 0;
    check("invalid base-target config fails before division",
          !resolvePearlTarget(canonicalJob, canonical, badConfig, &resolved,
                              &error, &code) &&
              code == PearlJobContextError::InvalidMiningConfig);
    check("final target does not spuriously depend on penalty config",
          resolvePearlTarget(finalJob, finalContext, badConfig, &resolved,
                             &error, &code) &&
              sameU256(resolved, pearl::U256::fromLimbs(finalJob.target)));

    Job overflowJob = canonicalJob;
    for (uint64_t &limb : overflowJob.target)
        limb = std::numeric_limits<uint64_t>::max();
    resolved.v[0] = 0x1122334455667788ULL;
    resolved.v[1] = 0x2233445566778899ULL;
    resolved.v[2] = 0x33445566778899aaULL;
    resolved.v[3] = 0x445566778899aabbULL;
    const pearl::U256 beforeFailure = resolved;
    check("overflowing base-target penalty fails closed",
          !resolvePearlTarget(overflowJob, canonical, config, &resolved, &error,
                              &code) &&
              code == PearlJobContextError::TargetOverflow &&
              sameU256(resolved, beforeFailure));
    Job maximumFinal = finalJob;
    for (uint64_t &limb : maximumFinal.target)
        limb = std::numeric_limits<uint64_t>::max();
    check("maximum final adjusted bound copies without multiplication",
          resolvePearlTarget(maximumFinal, finalContext, config, &resolved,
                             &error, &code) &&
              sameU256(resolved, pearl::U256::fromLimbs(maximumFinal.target)));
    check("synthetic zero final bound remains the no-share sentinel",
          resolvePearlTarget(syntheticJob, decoded, config, &resolved, &error,
                             &code) &&
              resolved.v[0] == 0 && resolved.v[1] == 0 &&
              resolved.v[2] == 0 && resolved.v[3] == 0);

    PearlJobContext tooLong = canonical;
    tooLong.transportJobId.assign(kMaxPearlTransportJobIdBytes + 1, 'x');
    refused = "must-clear";
    check("oversized job id fails with stable resource error and clear output",
          !encodePearlJobContext(tooLong, &refused, &error, &code) &&
              code == PearlJobContextError::TransportJobIdTooLong &&
              refused.empty());

    PearlJobContext exactMax = canonical;
    exactMax.transportJobId.assign(kMaxPearlTransportJobIdBytes, 'm');
    exactMax.transportJobId[17] = '\0';
    std::string exactMaxEnvelope;
    check("exact maximum-size opaque job id encodes",
          encodePearlJobContext(exactMax, &exactMaxEnvelope, &error, &code));
    Job exactMaxJob = jobFor(exactMax);
    check("exact maximum-size opaque job id validates when bindings match",
          validatePearlJobContext(exactMaxJob, nullptr, &decoded, &error,
                                  &code) &&
              pearlJobContextEqual(exactMax, decoded));
    Job oversizedGenericId = exactMaxJob;
    oversizedGenericId.jobId.push_back('x');
    PearlJobContext oversizedUntouched = canonical;
    const PearlJobContext oversizedUntouchedBefore = oversizedUntouched;
    check("oversized generic job id cannot bypass canonical binding",
          !validatePearlJobContext(oversizedGenericId, nullptr,
                                   &oversizedUntouched, &error, &code) &&
              code == PearlJobContextError::ContextMismatch &&
              !error.empty() &&
              pearlJobContextEqual(oversizedUntouched,
                                   oversizedUntouchedBefore));

    PearlJobContext aliasedInput = canonical;
    const std::string aliasedInputBefore = aliasedInput.transportJobId;
    check("encoder rejects output aliasing opaque input before mutation",
          !encodePearlJobContext(aliasedInput, &aliasedInput.transportJobId,
                                 &error, &code) &&
              code == PearlJobContextError::AliasedOutput &&
              aliasedInput.transportJobId == aliasedInputBefore);
    std::string aliasedOutputs = "must-not-change";
    check("encoder rejects aliased success/error outputs before mutation",
          !encodePearlJobContext(canonical, &aliasedOutputs, &aliasedOutputs,
                                 &code) &&
              code == PearlJobContextError::AliasedOutput &&
              aliasedOutputs == "must-not-change");
    aliasedInput = canonical;
    std::string separateOutput = "must-not-change";
    check("encoder rejects error output aliasing opaque input before mutation",
          !encodePearlJobContext(aliasedInput, &separateOutput,
                                 &aliasedInput.transportJobId, &code) &&
              code == PearlJobContextError::AliasedOutput &&
              separateOutput == "must-not-change" &&
              aliasedInput.transportJobId == aliasedInputBefore);
    check("null success output cannot mutate aliased opaque input through error",
          !encodePearlJobContext(aliasedInput, nullptr,
                                 &aliasedInput.transportJobId, &code) &&
              code == PearlJobContextError::AliasedOutput &&
              aliasedInput.transportJobId == aliasedInputBefore);

    refused = "must-clear";
    pearlSetJobContextAllocationCountdownForTest(0);
    check("deterministic encoder allocation failure is contained",
          !encodePearlJobContext(canonical, &refused, &error, &code) &&
              code == PearlJobContextError::AllocationFailure &&
              refused.empty());
    PearlJobContext allocationUntouched = canonical;
    pearlSetJobContextAllocationCountdownForTest(0);
    check("deterministic decoder ID-allocation failure is transactional",
          !decodePearlJobContext(canonicalJob, &allocationUntouched, &error,
                                 &code) &&
              code == PearlJobContextError::AllocationFailure &&
              pearlJobContextEqual(allocationUntouched, canonical));
    allocationUntouched = canonical;
    pearlSetJobContextAllocationCountdownForTest(1);
    check("deterministic canonical-reencode failure is transactional",
          !decodePearlJobContext(canonicalJob, &allocationUntouched, &error,
                                 &code) &&
              code == PearlJobContextError::AllocationFailure &&
              pearlJobContextEqual(allocationUntouched, canonical));
    pearlDisableJobContextAllocationFailureForTest();
    Job huge = canonicalJob;
    huge.extra.assign(kMaxPearlJobEnvelopeBytes + 1, 'x');
    PearlJobContext untouched = canonical;
    check("oversized envelope fails before parsing",
          !decodePearlJobContext(huge, &untouched, &error, &code) &&
              code == PearlJobContextError::EnvelopeTooLong &&
              pearlJobContextEqual(untouched, canonical));
    refused = "must-clear";
    check("unknown enum cannot be encoded and clears output",
          !(mutated = canonical,
            mutated.network = static_cast<PearlNetwork>(255),
            encodePearlJobContext(mutated, &refused, &error, &code)) &&
              code == PearlJobContextError::InvalidNetwork && refused.empty());
    check("null encoder output fails safely",
          !encodePearlJobContext(canonical, nullptr, &error, &code) &&
              code == PearlJobContextError::NullOutput);
    check("null decoder output fails safely",
          !decodePearlJobContext(canonicalJob, nullptr, &error, &code) &&
              code == PearlJobContextError::NullOutput);
    check("null target output fails safely",
          !resolvePearlTarget(canonicalJob, canonical, config, nullptr, &error,
                              &code) &&
              code == PearlJobContextError::NullOutput);

    if (failures != 0) {
        fprintf(stderr, "%d Pearl job-context checks failed\n", failures);
        return 1;
    }
    printf("PASS: Pearl job context, fork schedule, and target semantics\n");
    return 0;
}
