// Host-only regression test for the live rate labels.
//
// Pearl candidates are tiles, not hashes. Keep that distinction visible in the
// interactive readout without needing a CUDA device or a running work source.

#include <cstdio>
#include <cstring>
#include <string>

#include <unistd.h>

#include "../src/core/telemetry.h"

using om::GpuTelemetry;
using om::MinerStats;

static std::string readoutFor(const char *algo, double macsPerUnit = 0,
                              double rate = 0, bool tty = true,
                              const char *gpuUuid =
                                  "GPU-00112233-4455-6677-8899-aabbccddeeff",
                              const om::BenchmarkIdentity *identity = nullptr) {
    MinerStats stats;
    stats.algo = algo;
    stats.uptimeNanoseconds = 19490123456ULL;
    stats.uptimeSeconds = static_cast<double>(stats.uptimeNanoseconds) / 1e9;
    stats.hashrate = rate > 0 ? rate : 123.45;
    // Deliberately NOT the same as the rate. Both are printed on one line, so
    // an assertion looking for the converted rate would be satisfied by the
    // average and pass a build where only the rate is wrong - which is how a
    // mutation of shownRate alone survived this test once.
    stats.hashrateAvg = rate > 0 ? rate / 2.0 : 120.0;
    stats.macsPerUnit = macsPerUnit;
    stats.workUnitsPerAttempt = 1048576;
    stats.totalNonces = 7340032;
    stats.totalAttempts = 7;
    stats.attemptsPerSecond = 2.5;
    stats.attemptsPerSecondAvg = 2.0;
    stats.history = {110.0, 123.45};
    if (identity) stats.benchmarkIdentity = *identity;

    GpuTelemetry telemetry;
    telemetry.valid = true;
    telemetry.powerMilliwatts = 200000;
    telemetry.temperatureC = 60;
    telemetry.fanPercent = 40;
    if (gpuUuid) telemetry.deviceUuid = gpuUuid;

    FILE *capture = tmpfile();
    if (!capture) return "";
    const int saved = dup(fileno(stdout));
    if (saved < 0 || dup2(fileno(capture), fileno(stdout)) < 0) return "";

    om::g_asciiOnly = true;
    om::printReadout(stats, telemetry, tty);
    fflush(stdout);
    dup2(saved, fileno(stdout));
    close(saved);

    rewind(capture);
    std::string output;
    char buffer[256];
    while (const size_t n = fread(buffer, 1, sizeof(buffer), capture))
        output.append(buffer, n);
    fclose(capture);
    return output;
}

int main() {
    const unsigned char uuidBytes[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    const std::string uuid = om::nvidiaGpuUuid(uuidBytes, sizeof(uuidBytes));
    if (uuid != "GPU-00112233-4455-6677-8899-aabbccddeeff" ||
        !om::gpuUuidEqual(uuid, "gpu-00112233-4455-6677-8899-AABBCCDDEEFF") ||
        om::gpuUuidEqual(uuid, "GPU-00000000-0000-0000-0000-000000000000") ||
        !om::nvidiaGpuUuid(uuidBytes, sizeof(uuidBytes) - 1).empty()) {
        fprintf(stderr, "GPU UUID formatting/comparison regression\n");
        return 1;
    }

    // The shipped Pearl configuration: a 16x16 tile of a product whose every
    // element is a length-2048 dot product.
    const double kPearlMacs = 16.0 * 16.0 * 2048.0;   // 524,288

    const std::string pearl = readoutFor("pearl-pow", kPearlMacs);
    const std::string ergo = readoutFor("autolykos2");

    const bool pearlUnits = pearl.find("T MAC/s") != std::string::npos &&
                            pearl.find("T MAC/W") != std::string::npos &&
                            pearl.find("MH/s") == std::string::npos &&
                            pearl.find("MC/s") == std::string::npos;
    const bool hashUnits = ergo.find("MH/s") != std::string::npos &&
                           ergo.find("MH/W") != std::string::npos &&
                           ergo.find("T MAC") == std::string::npos;

    if (!pearlUnits || !hashUnits) {
        fprintf(stderr, "telemetry rate unit regression\n");
        return 1;
    }

    // The label alone is not the point. A wrong multiplier prints a
    // confident, comparable-looking number that is off by orders of
    // magnitude, which is worse than an honest MC/s. So check the arithmetic.
    //
    // 340 M candidates/s on a 4090 is 340e6 * 524288 = 1.783e14 MAC/s
    // = 178.26 T MAC/s. That figure sits against hashrate.no's 187.8 for a
    // 5080, which is the right order for these two cards.
    const std::string fast = readoutFor("pearl-pow", kPearlMacs, 340.0);
    if (fast.find("178.26") == std::string::npos) {
        fprintf(stderr, "T MAC/s conversion wrong: expected 178.26 in\n%s",
                fast.c_str());
        return 1;
    }

    // And the JSON, which is what the GUI and any monitoring actually read.
    const std::string json = readoutFor("pearl-pow", kPearlMacs, 340.0, false);
    const bool jsonOk =
        json.find("\"uptime_s\":19,\"uptime_ns\":19490123456,") != std::string::npos &&
        json.find("\"unit\":\"T MAC/s\"") != std::string::npos &&
        json.find("\"gpu_uuid\":\"GPU-00112233-4455-6677-8899-aabbccddeeff\"") !=
            std::string::npos &&
        json.find("\"eff_unit\":\"T MAC/W\"") != std::string::npos &&
        json.find("\"rate\":178.26") != std::string::npos &&
        json.find("\"rate_avg\":89.13") != std::string::npos &&
        // The legacy keys stay for one release and must agree with the new
        // ones, or a consumer reading the old name gets a different number
        // from one reading the new name.
        json.find("\"mhs\":178.26") != std::string::npos &&
        json.find("\"eff_mh_w\":") != std::string::npos &&
        // The raw counter survives for diagnosis: 340 M/s = 3.4e8.
        json.find("\"candidates_per_s\":340000000") != std::string::npos &&
        json.find("\"candidates_total\":7340032") != std::string::npos &&
        json.find("\"attempts_per_s\":2.500000") != std::string::npos &&
        json.find("\"attempts_per_s_avg\":2.000000") != std::string::npos &&
        json.find("\"attempts_total\":7") != std::string::npos &&
        json.find("\"candidates_per_attempt\":1048576") != std::string::npos;
    if (!jsonOk) {
        fprintf(stderr, "telemetry JSON shape regression:\n%s", json.c_str());
        return 1;
    }

    if (json.find("benchmark_identity") != std::string::npos) {
        fprintf(stderr, "ordinary telemetry unexpectedly has formal identity\n");
        return 1;
    }

    om::BenchmarkIdentity formal;
    formal.valid = true;
    formal.formal = true;
    formal.schema = om::kPearlBenchmarkSchema;
    formal.fixtureId = om::kPearlBenchmarkFixture;
    formal.startingNonce = UINT64_C(0x0123456789abcdef);
    formal.shapeId = "m4096-n65536";
    formal.m = 4096;
    formal.n = 65536;
    formal.k = om::kPearlBenchmarkK;
    formal.rank = om::kPearlBenchmarkRank;
    formal.kernelId = "cuda-ptx-mma-s8-v1";
    formal.tileId = "wm2-wn4-tm2-tn2-bk64-st3-gm8";
    const std::string formalJson = readoutFor(
        "pearl-pow", kPearlMacs, 340.0, false,
        "GPU-00112233-4455-6677-8899-aabbccddeeff", &formal);
    if (formalJson.find(
            "\"benchmark_identity\":{\"schema\":"
            "\"pearl-benchmark-identity-v1\",\"formal\":true,"
            "\"fixture_id\":\"pearl-synthetic-v1\","
            "\"starting_nonce\":\"0123456789abcdef\","
            "\"shape\":{\"id\":\"m4096-n65536\",\"m\":4096,"
            "\"n\":65536,\"k\":2048,\"rank\":128},"
            "\"kernel_id\":\"cuda-ptx-mma-s8-v1\","
            "\"tile_id\":\"wm2-wn4-tm2-tn2-bk64-st3-gm8\"}") ==
        std::string::npos) {
        fprintf(stderr, "formal telemetry identity regression:\n%s",
                formalJson.c_str());
        return 1;
    }

    const std::string noUuid = readoutFor("pearl-pow", kPearlMacs, 340.0,
                                          false, nullptr);
    const std::string longUuid(95, 'a');
    const std::string longUuidJson = readoutFor(
        "pearl-pow", kPearlMacs, 340.0, false, longUuid.c_str());
    if (noUuid.find("\"gpu_uuid\":null") == std::string::npos ||
        longUuidJson.find("\"gpu_uuid\":\"" + longUuid + "\"") ==
            std::string::npos) {
        fprintf(stderr, "GPU UUID JSON null/length regression\n");
        return 1;
    }

    // A hashing algorithm must not grow a MAC unit or the raw candidate key.
    const std::string ergoJson = readoutFor("autolykos2", 0, 217.0, false);
    if (ergoJson.find("\"unit\":\"MH/s\"") == std::string::npos ||
        ergoJson.find("candidates_per_s") != std::string::npos ||
        ergoJson.find("attempts_per_s") != std::string::npos ||
        ergoJson.find("\"rate\":217.00") == std::string::npos) {
        fprintf(stderr, "hashing algorithms must stay MH/s:\n%s", ergoJson.c_str());
        return 1;
    }

    puts("telemetry unit labels: ok");
    return 0;
}
