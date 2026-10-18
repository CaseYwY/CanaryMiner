// Upstream BLAKE3 empty-input default-length vectors, hash and keyed_hash.
// https://github.com/BLAKE3-team/BLAKE3/blob/1.8.2/test_vectors/test_vectors.json
// No GPU, network, Python package or generated oracle is needed to run this.
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../src/algos/pearl-pow/job.h"

static bool matches(const uint8_t *digest, const char *hex) {
    const char *digits = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i)
        if (digits[digest[i] >> 4] != hex[2*i] ||
            digits[digest[i] & 15] != hex[2*i+1]) return false;
    return true;
}
int main() {
    using namespace om::pearl;
    const uint8_t key[] = "whats the Elvish word for friend";
    const char *expected[] = {
        "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262",
        "92b2b75604ed3c761f9d6f62392c8a9227ad0ea3f09573e783f1498a4ed60d26b"};
    static_assert(sizeof(key) == 33, "32-byte official key plus terminator");
    std::vector<uint8_t> empty;
    uint8_t sentinel = 0x5a;
    const uint8_t *inputs[] = {nullptr, empty.data(), &sentinel};
    for (unsigned mode = 0; mode < 2; ++mode) {
        for (const auto *input : inputs) {
            std::array<uint8_t, 32> hash{}, chunk{};
            b3::hash(mode ? key : nullptr, input, 0, hash.data());
            b3::chunkCv(mode ? key : nullptr, input, 0, 0, true, chunk.data());
            if (!matches(hash.data(), expected[mode]) || hash != chunk) return 1;
        }
    }
    uint32_t block[16];
    b3::loadBlock(nullptr, 0, block);
    for (const auto word : block) if (word) return 1;
    if (sentinel != 0x5a || !empty.empty()) return 1;
    std::puts("PASS: Pearl BLAKE3 null/empty inputs match upstream keyed/unkeyed vectors");
    return 0;
}
