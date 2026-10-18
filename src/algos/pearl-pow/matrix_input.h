#pragma once
#include <stdint.h>

// Input distribution only: Pearl noise, dense arithmetic and proof format stay intact.
#ifndef CANARY_PEARL_COMPRESSIBLE_MATRICES
#define CANARY_PEARL_COMPRESSIBLE_MATRICES 0
#endif
#if CANARY_PEARL_COMPRESSIBLE_MATRICES != 0 && CANARY_PEARL_COMPRESSIBLE_MATRICES != 1
#error "CANARY_PEARL_COMPRESSIBLE_MATRICES must be 0 or 1"
#endif
#if defined(__CUDACC__)
#define PEARL_MATRIX_HD __host__ __device__
#else
#define PEARL_MATRIX_HD
#endif
namespace om::pearl::matrix_input {
constexpr bool compressible = CANARY_PEARL_COMPRESSIBLE_MATRICES == 1;
constexpr const char *profile = compressible ? "sparse8-per1k-v1" : "uniform-int7-v1";
// Keep one eight-byte word per chunk at a seed-dependent location. Chunks/rows
// do not all select the same columns. The retained word uses the original
// seed+word generator. Existing A/B domain separation and nonce allocation stay.
PEARL_MATRIX_HD inline uint64_t choiceHash(uint64_t value) {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}
PEARL_MATRIX_HD inline bool keepWord(uint64_t seed, uint64_t word) {
    const uint64_t chunk = word >> 7;
    const uint64_t selected = choiceHash(seed ^ 0xC13FA9A902A6328FULL ^ chunk) & 127;
    return (word & 127) == selected;
}
}
#undef PEARL_MATRIX_HD
