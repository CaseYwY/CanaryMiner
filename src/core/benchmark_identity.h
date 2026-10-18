#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace om {

inline constexpr const char kPearlBenchmarkSchema[] =
    "pearl-benchmark-identity-v1";
inline constexpr const char kPearlBenchmarkFixture[] = "pearl-synthetic-v1";
inline constexpr uint64_t kPearlBenchmarkEpoch = 1851444;
inline constexpr uint32_t kPearlBenchmarkK = 2048;
inline constexpr uint32_t kPearlBenchmarkRank = 128;
inline constexpr uint32_t kPearlBenchmarkHeaderBytes = 76;
inline constexpr int kPearlBenchmarkCertificate = 1;
inline constexpr uint64_t kPearlFormalBenchmarkBatch = UINT64_C(4194304);
inline constexpr uint32_t kPearlFormalBenchmarkReportSeconds = 5;

inline void fillPearlBenchmarkHeader(
        uint8_t (&header)[kPearlBenchmarkHeaderBytes]) {
    for (uint32_t i = 0; i < kPearlBenchmarkHeaderBytes; ++i)
        header[i] = static_cast<uint8_t>(0xab ^ i);
}

struct FormalBenchmarkRequest {
    bool enabled = false;
    uint64_t startingNonce = 0;
    std::string shapeId;
    std::string kernelId;
    std::string tileId;
};

struct BenchmarkIdentity {
    bool valid = false;
    bool formal = false;
    std::string schema;
    std::string fixtureId;
    uint64_t startingNonce = 0;
    std::string shapeId;
    uint32_t m = 0, n = 0, k = 0, rank = 0;
    std::string kernelId;
    std::string tileId;
};

inline bool canonicalBenchmarkId(std::string_view id) {
    if (id.empty()) return false;
    const auto lowerOrDigit = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    };
    if (!lowerOrDigit(id.front())) return false;
    for (char c : id)
        if (!lowerOrDigit(c) && c != '.' && c != '_' && c != '-') return false;
    return true;
}

/** Accept only 0x followed by exactly sixteen hexadecimal digits. */
inline bool parseBenchmarkStartingNonce(std::string_view text,
                                        uint64_t *value) {
    if (!value || text.size() != 18 || text[0] != '0' || text[1] != 'x')
        return false;
    uint64_t parsed = 0;
    for (size_t i = 2; i < text.size(); ++i) {
        const char c = text[i];
        uint8_t digit = 0;
        if (c >= '0' && c <= '9') digit = static_cast<uint8_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<uint8_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = static_cast<uint8_t>(c - 'A' + 10);
        else
            return false;
        parsed = (parsed << 4) | digit;
    }
    *value = parsed;
    return true;
}

/** Strict unsigned decimal parser for canonical formal numeric controls. */
inline bool parseFormalUnsignedDecimal(std::string_view text,
                                       uint64_t *value) {
    if (!value || text.empty()) return false;
    uint64_t parsed = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (parsed > (UINT64_MAX - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    *value = parsed;
    return true;
}

inline std::string formatBenchmarkStartingNonce(uint64_t value) {
    char out[17];
    std::snprintf(out, sizeof(out), "%016llx",
                  static_cast<unsigned long long>(value));
    return out;
}

/** Whether count consecutive nonces starting at first can be measured once. */
inline bool formalBenchmarkNonceRangeFits(uint64_t first, uint64_t count) {
    return count > 0 && count - 1 <= UINT64_MAX - first;
}

/** Whether advancing by count after that range avoids wrapping to a reuse. */
inline bool formalBenchmarkNonceCanAdvance(uint64_t first, uint64_t count) {
    return count > 0 && count <= UINT64_MAX - first;
}

inline bool advanceFormalBenchmarkNonce(uint64_t first, uint64_t attempts,
                                        uint64_t *next) {
    if (!next || !formalBenchmarkNonceCanAdvance(first, attempts)) return false;
    *next = first + attempts;
    return true;
}

inline bool validFormalBenchmarkRequest(const FormalBenchmarkRequest &request) {
    return request.enabled && canonicalBenchmarkId(request.shapeId) &&
           canonicalBenchmarkId(request.kernelId) &&
           canonicalBenchmarkId(request.tileId);
}

inline bool validBenchmarkIdentity(const BenchmarkIdentity &identity) {
    return identity.valid && identity.formal &&
           identity.schema == kPearlBenchmarkSchema &&
           identity.fixtureId == kPearlBenchmarkFixture &&
           canonicalBenchmarkId(identity.shapeId) &&
           canonicalBenchmarkId(identity.kernelId) &&
           canonicalBenchmarkId(identity.tileId) && identity.m > 0 &&
           identity.n > 0 && identity.k == kPearlBenchmarkK &&
           identity.rank == kPearlBenchmarkRank &&
           identity.m % 16 == 0 && identity.n % 16 == 0 &&
           identity.shapeId == "m" + std::to_string(identity.m) + "-n" +
                                   std::to_string(identity.n);
}

inline bool benchmarkIdentityMatches(const FormalBenchmarkRequest &request,
                                     const BenchmarkIdentity &identity) {
    return validFormalBenchmarkRequest(request) &&
           validBenchmarkIdentity(identity) &&
           identity.startingNonce == request.startingNonce &&
           identity.shapeId == request.shapeId &&
           identity.kernelId == request.kernelId &&
           identity.tileId == request.tileId;
}

/** Canonical JSON; IDs are emitted only after the restricted grammar passes. */
inline std::string benchmarkIdentityJson(const BenchmarkIdentity &identity) {
    if (!validBenchmarkIdentity(identity)) return {};
    return std::string("{\"schema\":\"") + identity.schema +
           "\",\"formal\":true,\"fixture_id\":\"" + identity.fixtureId +
           "\",\"starting_nonce\":\"" +
           formatBenchmarkStartingNonce(identity.startingNonce) +
           "\",\"shape\":{\"id\":\"" + identity.shapeId +
           "\",\"m\":" + std::to_string(identity.m) +
           ",\"n\":" + std::to_string(identity.n) +
           ",\"k\":" + std::to_string(identity.k) +
           ",\"rank\":" + std::to_string(identity.rank) +
           "},\"kernel_id\":\"" + identity.kernelId +
           "\",\"tile_id\":\"" + identity.tileId + "\"}";
}

}  // namespace om
