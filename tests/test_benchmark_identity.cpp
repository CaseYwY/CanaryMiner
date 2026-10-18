#include <cstdint>
#include <cstdio>
#include <string>

#include "../src/core/run.h"

namespace {

int failures = 0;

void check(const char *name, bool condition) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

om::FormalBenchmarkRequest request() {
    om::FormalBenchmarkRequest value;
    value.enabled = true;
    value.startingNonce = UINT64_C(0x0123456789abcdef);
    value.shapeId = "m4096-n65536";
    value.kernelId = "cuda-ptx-mma-s8-v1";
    value.tileId = "wm2-wn4-tm2-tn2-bk64-st3-gm8";
    return value;
}

om::BenchmarkIdentity identity() {
    const om::FormalBenchmarkRequest selected = request();
    om::BenchmarkIdentity value;
    value.valid = true;
    value.formal = true;
    value.schema = om::kPearlBenchmarkSchema;
    value.fixtureId = om::kPearlBenchmarkFixture;
    value.startingNonce = selected.startingNonce;
    value.shapeId = selected.shapeId;
    value.m = 4096;
    value.n = 65536;
    value.k = om::kPearlBenchmarkK;
    value.rank = om::kPearlBenchmarkRank;
    value.kernelId = selected.kernelId;
    value.tileId = selected.tileId;
    return value;
}

}  // namespace

int main() {
    check("canonical IDs accepted",
          om::canonicalBenchmarkId("a") &&
              om::canonicalBenchmarkId("cuda-ptx_mma.s8-v1") &&
              om::canonicalBenchmarkId("m4096-n65536"));
    check("malformed IDs rejected",
          !om::canonicalBenchmarkId("") &&
              !om::canonicalBenchmarkId("-leading") &&
              !om::canonicalBenchmarkId("Upper") &&
              !om::canonicalBenchmarkId("has/slash") &&
              !om::canonicalBenchmarkId("has space"));

    uint64_t nonce = 0;
    check("strict nonce accepted and normalized",
          om::parseBenchmarkStartingNonce("0x0123456789ABCDEF", &nonce) &&
              nonce == UINT64_C(0x0123456789abcdef) &&
              om::formatBenchmarkStartingNonce(nonce) ==
                  "0123456789abcdef");
    check("nonce parser consumes the exact full token",
          !om::parseBenchmarkStartingNonce("0123456789abcdef", &nonce) &&
              !om::parseBenchmarkStartingNonce("0X0123456789abcdef", &nonce) &&
              !om::parseBenchmarkStartingNonce("0x123", &nonce) &&
              !om::parseBenchmarkStartingNonce("0x0123456789abcdef0", &nonce) &&
              !om::parseBenchmarkStartingNonce("0x0123456789abcdeg", &nonce) &&
              !om::parseBenchmarkStartingNonce("0x0123456789abcdefjunk", &nonce));

    uint64_t decimal = 0;
    check("formal decimal controls consume the exact token",
          om::parseFormalUnsignedDecimal("4194304", &decimal) &&
              decimal == om::kPearlFormalBenchmarkBatch &&
              !om::parseFormalUnsignedDecimal("", &decimal) &&
              !om::parseFormalUnsignedDecimal("+5", &decimal) &&
              !om::parseFormalUnsignedDecimal("5junk", &decimal) &&
              !om::parseFormalUnsignedDecimal("18446744073709551616", &decimal));
    check("canonical formal protocol constants stay pinned",
          om::kPearlFormalBenchmarkBatch == UINT64_C(4194304) &&
              om::kPearlFormalBenchmarkReportSeconds == 5);

    om::RunOptions canonical;
    canonical.formalBenchmark = request();
    canonical.bench = true;
    canonical.plain = true;
    canonical.backendLabel = "CUDA";
    canonical.batch = om::kPearlFormalBenchmarkBatch;
    canonical.reportSeconds = om::kPearlFormalBenchmarkReportSeconds;
    canonical.benchEpoch = om::kPearlBenchmarkEpoch;
    check("pure runtime guard accepts only the canonical formal values",
          om::canonicalFormalBenchmarkRunOptions(canonical, "pearl-pow"));
    om::RunOptions variant = canonical;
    variant.formalBenchmark.enabled = false;
    check("runtime guard requires an enabled formal request",
          !om::canonicalFormalBenchmarkRunOptions(variant, "pearl-pow"));
    variant = canonical;
    variant.formalBenchmark.shapeId = "Malformed";
    check("runtime guard rejects malformed formal selectors",
          !om::canonicalFormalBenchmarkRunOptions(variant, "pearl-pow"));
    variant = canonical;
    variant.bench = false;
    check("runtime guard requires benchmark mode",
          !om::canonicalFormalBenchmarkRunOptions(variant, "pearl-pow"));
    variant = canonical;
    variant.plain = false;
    check("runtime guard rejects non-plain mode",
          !om::canonicalFormalBenchmarkRunOptions(variant, "pearl-pow"));
    variant = canonical;
    variant.batch++;
    check("runtime guard rejects a different batch",
          !om::canonicalFormalBenchmarkRunOptions(variant, "pearl-pow"));
    variant = canonical;
    variant.reportSeconds++;
    check("runtime guard rejects a different report interval",
          !om::canonicalFormalBenchmarkRunOptions(variant, "pearl-pow"));
    variant = canonical;
    variant.benchEpoch++;
    check("runtime guard rejects a different fixture height",
          !om::canonicalFormalBenchmarkRunOptions(variant, "pearl-pow"));
    variant = canonical;
    variant.memOffsetMhz = 1;
    check("runtime guard rejects clock control",
          !om::canonicalFormalBenchmarkRunOptions(variant, "pearl-pow"));
    variant = canonical;
    variant.memOc = true;
    check("runtime guard rejects automatic memory overclock",
          !om::canonicalFormalBenchmarkRunOptions(variant, "pearl-pow"));
    variant = canonical;
    variant.benchEpochSeconds = 1;
    check("runtime guard rejects epoch cycling",
          !om::canonicalFormalBenchmarkRunOptions(variant, "pearl-pow"));
    variant = canonical;
    variant.backendLabel = "Vulkan";
    check("runtime guard rejects the wrong algorithm or backend",
          !om::canonicalFormalBenchmarkRunOptions(canonical, "kawpow") &&
              !om::canonicalFormalBenchmarkRunOptions(variant, "pearl-pow"));

    uint8_t header[om::kPearlBenchmarkHeaderBytes] = {};
    om::fillPearlBenchmarkHeader(header);
    bool fixtureExact = true;
    for (uint32_t i = 0; i < om::kPearlBenchmarkHeaderBytes; ++i)
        fixtureExact &= header[i] == static_cast<uint8_t>(0xab ^ i);
    check("fixture header is exact and single-sourced", fixtureExact);

    const om::FormalBenchmarkRequest selected = request();
    const om::BenchmarkIdentity observed = identity();
    check("request and identity match every selector",
          om::validFormalBenchmarkRequest(selected) &&
              om::validBenchmarkIdentity(observed) &&
              om::benchmarkIdentityMatches(selected, observed));
    om::BenchmarkIdentity changed = observed;
    changed.tileId += "-prefix";
    check("selector equality is exact, not prefix-based",
          !om::benchmarkIdentityMatches(selected, changed));
    changed = observed;
    changed.fixtureId = "pearl-synthetic-v2";
    check("fixture mismatch fails closed",
          !om::benchmarkIdentityMatches(selected, changed));
    changed = observed;
    changed.k++;
    check("fixed shape semantics fail closed",
          !om::validBenchmarkIdentity(changed));
    changed = observed;
    changed.m = 2048;
    check("shape ID and numeric dimensions cannot disagree",
          !om::validBenchmarkIdentity(changed));

    const std::string json = om::benchmarkIdentityJson(observed);
    check("identity JSON is canonical and complete",
          json ==
              "{\"schema\":\"pearl-benchmark-identity-v1\",\"formal\":true,"
              "\"fixture_id\":\"pearl-synthetic-v1\","
              "\"starting_nonce\":\"0123456789abcdef\","
              "\"shape\":{\"id\":\"m4096-n65536\",\"m\":4096,"
              "\"n\":65536,\"k\":2048,\"rank\":128},"
              "\"kernel_id\":\"cuda-ptx-mma-s8-v1\","
              "\"tile_id\":\"wm2-wn4-tm2-tn2-bk64-st3-gm8\"}");
    check("invalid identity never serializes",
          om::benchmarkIdentityJson({}).empty());
    check("production shape has 1,048,576 candidates",
          (UINT64_C(4096) / 16) * (UINT64_C(65536) / 16) == 1048576);

    check("nonce ranges accept their last value but never wrap",
          om::formalBenchmarkNonceRangeFits(UINT64_MAX, 1) &&
              !om::formalBenchmarkNonceRangeFits(UINT64_MAX, 2) &&
              !om::formalBenchmarkNonceRangeFits(0, 0) &&
              !om::formalBenchmarkNonceCanAdvance(UINT64_MAX, 1) &&
              om::formalBenchmarkNonceCanAdvance(UINT64_MAX - 1, 1));

    uint64_t firstRun = selected.startingNonce;
    uint64_t secondRun = selected.startingNonce;
    const uint64_t candidateCounts[] = {UINT64_C(1048576), UINT64_C(4194304)};
    bool sameAttemptSequence = firstRun == selected.startingNonce;
    for (uint64_t ignoredCandidates : candidateCounts) {
        (void)ignoredCandidates;
        uint64_t nextFirst = 0, nextSecond = 0;
        sameAttemptSequence &=
            om::advanceFormalBenchmarkNonce(firstRun, 4, &nextFirst) &&
            om::advanceFormalBenchmarkNonce(secondRun, 4, &nextSecond) &&
            nextFirst == nextSecond;
        firstRun = nextFirst;
        secondRun = nextSecond;
    }
    check("formal cursor starts exactly and advances by attempts, not candidates",
          sameAttemptSequence &&
              firstRun == selected.startingNonce + UINT64_C(8));
    uint64_t wrapped = 0;
    check("formal cursor refuses an advance that would wrap",
          !om::advanceFormalBenchmarkNonce(UINT64_MAX, 1, &wrapped));

    if (failures) return 1;
    std::puts("Pearl benchmark identity: ok");
    return 0;
}
