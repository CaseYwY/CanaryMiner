#include <stdint.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>

#include "algos/pearl-pow/solution_identity.h"

using namespace om;

namespace {

int failures = 0;

struct FailingBufferFactory {
    uint8_t *operator()(std::size_t) const {
        throw std::bad_alloc();
    }
};

void check(const char *name, bool ok) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

bool isZero(const pearl::SolutionFingerprint &fingerprint) {
    for (uint8_t byte : fingerprint.bytes)
        if (byte != 0) return false;
    return true;
}

uint8_t hexNibble(char digit) {
    if (digit >= '0' && digit <= '9')
        return static_cast<uint8_t>(digit - '0');
    return static_cast<uint8_t>(digit - 'a' + 10);
}

bool matchesHex(const pearl::SolutionFingerprint &fingerprint,
                const char (&expected)[65]) {
    for (std::size_t i = 0; i < fingerprint.bytes.size(); ++i) {
        const uint8_t byte = static_cast<uint8_t>(
            (hexNibble(expected[2 * i]) << 4) |
            hexNibble(expected[2 * i + 1]));
        if (fingerprint.bytes[i] != byte) return false;
    }
    return expected[64] == '\0';
}

pearl::SolutionFingerprint fingerprint(const Solution &solution) {
    pearl::SolutionFingerprint result{};
    result.bytes.fill(0xa5);
    check("fingerprint computation succeeds",
          pearl::computeSolutionFingerprint(solution, &result));
    check("successful fingerprint is nonzero", !isZero(result));
    return result;
}

bool differs(const Solution &a, const Solution &b) {
    const pearl::SolutionFingerprint aFingerprint = fingerprint(a);
    const pearl::SolutionFingerprint bFingerprint = fingerprint(b);
    return !pearl::solutionFingerprintEqual(aFingerprint, bFingerprint);
}

}  // namespace

int main() {
    static_assert(noexcept(pearl::computeSolutionFingerprint(
                      std::declval<const Solution &>(),
                      static_cast<pearl::SolutionFingerprint *>(nullptr))),
                  "solution identity must contain allocation/hash failures");
    static_assert(pearl::solution_identity_detail::kFixedEncodingBytes == 73,
                  "canonical fixed prefix size drifted");

    Solution base{};
    base.nonce = UINT64_C(0x0123456789abcdef);
    base.hit[0] = 0;
    base.hit[1] = 1;
    base.hit[2] = UINT64_C(0x1122334455667788);
    base.hit[3] = std::numeric_limits<uint64_t>::max();
    const char proof[] = {0x41, 0x00, 0x42,
                          static_cast<char>(0x80),
                          static_cast<char>(0xff), 0x5a};
    base.extra.assign(proof, sizeof(proof));
    const pearl::SolutionFingerprint baseFingerprint = fingerprint(base);
    check("independent Python BLAKE3 vector is pinned",
          matchesHex(
              baseFingerprint,
              "4b8e6dd073195239750adc18571fbaffdde3c2709375be00fe813fd083a54920"));

    Solution mutation = base;
    mutation.nonce ^= UINT64_C(1) << 63;
    check("nonce mutation changes identity", differs(base, mutation));
    for (std::size_t limb = 0; limb < 4; ++limb) {
        mutation = base;
        mutation.hit[limb] ^= UINT64_C(1) << (limb * 11);
        check("every hit limb participates in identity",
              differs(base, mutation));
    }
    for (std::size_t byte = 0; byte < base.extra.size(); ++byte) {
        mutation = base;
        mutation.extra[byte] = static_cast<char>(
            static_cast<unsigned char>(mutation.extra[byte]) ^ 0x01u);
        check("every proof byte participates in identity",
              differs(base, mutation));
    }

    mutation = base;
    std::swap(mutation.hit[0], mutation.hit[1]);
    check("hit-limb order is canonical", differs(base, mutation));
    mutation = base;
    std::swap(mutation.extra[0], mutation.extra[2]);
    check("proof-byte order is canonical", differs(base, mutation));
    mutation = base;
    mutation.extra.pop_back();
    check("proof truncation changes length-bound identity",
          differs(base, mutation));
    mutation = base;
    mutation.extra.push_back('\0');
    check("proof extension changes length-bound identity",
          differs(base, mutation));

    Solution withoutEmbeddedNul = base;
    withoutEmbeddedNul.extra.erase(1, 1);
    check("embedded NUL is exact proof data",
          base.extra.size() == 6 && base.extra[1] == '\0' &&
              differs(base, withoutEmbeddedNul));

    Solution zero{};
    const pearl::SolutionFingerprint zeroFingerprint = fingerprint(zero);
    check("all-zero fields have a pinned canonical identity",
          matchesHex(
              zeroFingerprint,
              "5da8e4bbe8151f7b6d1eb7b44cd0fea2a6a1355a0c84e0884d0a27d8fc091177"));
    Solution maximum{};
    maximum.nonce = std::numeric_limits<uint64_t>::max();
    for (uint64_t &limb : maximum.hit)
        limb = std::numeric_limits<uint64_t>::max();
    check("maximum integer fields have a pinned canonical identity",
          matchesHex(
              fingerprint(maximum),
              "5f65b12ce9118185f967f5eb826b489ee4499988fc785506962c86244f975538"));

    Solution sameBytesDifferentBoundaryA{};
    sameBytesDifferentBoundaryA.nonce = UINT64_C(0x0100000000000000);
    sameBytesDifferentBoundaryA.extra = "x";
    Solution sameBytesDifferentBoundaryB{};
    sameBytesDifferentBoundaryB.nonce = 1;
    sameBytesDifferentBoundaryB.extra.assign("\0\0\0\0\0\0\0x", 8);
    check("fixed-width integers and proof length prevent field-boundary ambiguity",
          differs(sameBytesDifferentBoundaryA, sameBytesDifferentBoundaryB));

    Solution tokenTest = base;
    check("proof token defaults to the unbound sentinel",
          tokenTest.pearlProofToken == 0);
    tokenTest.pearlProofToken = UINT64_C(0xfedcba9876543210);
    check("opaque ownership token is not part of serialized proof identity",
          pearl::solutionFingerprintEqual(baseFingerprint,
                                           fingerprint(tokenTest)));
    Solution independent{};
    check("each Solution owns an independent zero-default token",
          independent.pearlProofToken == 0 &&
              tokenTest.pearlProofToken == UINT64_C(0xfedcba9876543210));

    Solution boundary{};
    boundary.extra.assign(pearl::kSolutionFingerprintMaxProofBytes, 'p');
    pearl::SolutionFingerprint boundaryFingerprint{};
    check("maximum admitted proof length hashes successfully",
          pearl::computeSolutionFingerprint(boundary, &boundaryFingerprint) &&
              !isZero(boundaryFingerprint));

    Solution oversized{};
    oversized.extra.assign(pearl::kSolutionFingerprintMaxProofBytes + 1, 'q');
    pearl::SolutionFingerprint refused{};
    refused.bytes.fill(0xa5);
    check("proof above resource limit is refused with zero output",
          !pearl::computeSolutionFingerprint(oversized, &refused) &&
              isZero(refused));
    refused.bytes.fill(0xa5);
    check("allocation failure is contained and clears output",
          !pearl::solution_identity_detail::
              computeSolutionFingerprintWithFactory(
                  base, &refused, FailingBufferFactory()) &&
              isZero(refused));
    check("null fingerprint output is refused safely",
          !pearl::computeSolutionFingerprint(base, nullptr));

    if (failures != 0) {
        std::fprintf(stderr, "%d Pearl solution-identity checks failed\n",
                     failures);
        return 1;
    }
    std::puts("PASS: Pearl solution identity is canonical and fail-closed");
    return 0;
}
