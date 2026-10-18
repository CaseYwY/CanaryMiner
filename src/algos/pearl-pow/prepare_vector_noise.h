#pragma once
#include <cstdint>

#if defined(__CUDACC__)
#define OM_PEARL_NOISE_HD __host__ __device__ __forceinline__
#else
#define OM_PEARL_NOISE_HD inline
#endif

namespace om { namespace pearl { namespace vector_noise {
// Experimental preparation implementation, deliberately absent from dispatch.
// A remains row-major M x 8192, and E_AL remains row-major M x 128.
constexpr uint32_t kColumns = 8192;
constexpr uint32_t kRank = 128;
constexpr uint32_t kMaxRows = 16384;
constexpr uint64_t kMaxBytes = uint64_t(kMaxRows) * kColumns;

OM_PEARL_NOISE_HD bool validBytes(uint64_t bytes) noexcept {
    return bytes > 0 && bytes <= kMaxBytes;
}
struct Word {
    uint64_t offset;
    uint32_t row, column, count;
};
OM_PEARL_NOISE_HD Word wordAt(uint64_t word, uint64_t bytes) noexcept {
    if (!validBytes(bytes) || word >= (bytes + 3) / 4) return {0, 0, 0, 0};
    const uint64_t offset = word * 4;
    const uint32_t remaining = static_cast<uint32_t>(bytes - offset);
    return {offset, static_cast<uint32_t>(word >> 11),
            static_cast<uint32_t>((word & 2047) << 2), remaining < 4 ? remaining : 4};
}
// Unsigned conversion defines wrap modulo 256, including negative sums.
// Treating a raw signed byte as unsigned before adding noise has the same
// low eight bits as the existing int8 A + signed E1 - signed E2 expression.
OM_PEARL_NOISE_HD uint8_t noiseByte(uint8_t raw, int8_t first, int8_t second) noexcept {
    return static_cast<uint8_t>(int32_t(raw) + int32_t(first) - int32_t(second));
}
OM_PEARL_NOISE_HD uint32_t packWord(uint32_t raw, const int8_t *rowNoise,
        const uint16_t *first, const uint16_t *second, uint32_t column) noexcept {
    uint32_t packed = 0;
#if defined(__CUDA_ARCH__)
#pragma unroll
#endif
    for (unsigned byte = 0; byte < 4; ++byte) {
        const uint8_t result = noiseByte(static_cast<uint8_t>(raw >> (byte * 8)),
            rowNoise[first[column + byte]], rowNoise[second[column + byte]]);
        packed |= uint32_t(result) << (byte * 8);
    }
    return packed;
}
}}} // namespace om::pearl::vector_noise
#undef OM_PEARL_NOISE_HD
