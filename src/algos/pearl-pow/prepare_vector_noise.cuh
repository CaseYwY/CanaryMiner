#pragma once
#include <cuda_runtime.h>
#include "prepare_vector_noise.h"

namespace om { namespace pearl {
// Default-off exact-K8192/rank128 candidate. No production translation unit
// includes this header. Four bytes/thread replace four scalar threads; row and
// column indexing use shifts/masks. No shared scratch or inter-block ordering.
// Inputs retain the scalar preparation contract: first/second entries < 128,
// and rowNoise owns ceil(bytes/8192)*128 bytes. Full mining bytes = M*8192.
// Prefix lengths and unaligned pointers intentionally use the same scalar tail
// path so tests can check boundaries without relying on a divisible size.
__global__ void applyNoiseA4K8192(const int8_t *__restrict__ A,
        const int8_t *__restrict__ rowNoise, const uint16_t *__restrict__ first,
        const uint16_t *__restrict__ second, int8_t *__restrict__ out, uint64_t bytes) {
    const uint64_t index = uint64_t(blockIdx.x) * blockDim.x + threadIdx.x;
    const vector_noise::Word word = vector_noise::wordAt(index, bytes);
    if (!word.count) return;
    const int8_t *noise = rowNoise + (uint64_t(word.row) << 7);
    if (word.count == 4 &&
        ((reinterpret_cast<uintptr_t>(A) | reinterpret_cast<uintptr_t>(out)) & 3) == 0) {
        const uint32_t raw = *reinterpret_cast<const uint32_t *>(A + word.offset);
        *reinterpret_cast<uint32_t *>(out + word.offset) =
            vector_noise::packWord(raw, noise, first, second, word.column);
    } else {
#pragma unroll
        for (unsigned byte = 0; byte < 4; ++byte) {
            if (byte < word.count) {
                reinterpret_cast<uint8_t *>(out)[word.offset + byte] = vector_noise::noiseByte(
                    reinterpret_cast<const uint8_t *>(A)[word.offset + byte],
                    noise[first[word.column + byte]], noise[second[word.column + byte]]);
            }
        }
    }
}
}} // namespace om::pearl
