// 64x64-warp-tile fused Pearl GEMM for SM120 (K8192 / rank 128 admitted by the host).
// CTA 128(M) x 256(N), grid (n/256, m/128), raster group selected per lineage; 256 threads (2x4 warps of
// 64x64), BK64, 4 cp.async stages, one barrier per K-block between the k32 halves, cross-block fragment prefetch,
// batched fold. The integer GEMM, transcript fold, and scan are bit-exact with the established implementation.
#pragma once
#include <cuda_pipeline.h>
#include "noisy_gemm.cuh"
#ifndef N64_FOLD
#define N64_FOLD 1
#endif
namespace om {
namespace pearl {
namespace n64 {
#ifndef N64_S
#define N64_S 4
#endif
#ifndef N64_BM
#define N64_BM 128
#endif
#ifndef N64_BN
#define N64_BN 256
#endif
#ifndef N64_GM
#define N64_GM 16
#endif
constexpr int BM = N64_BM, BN = N64_BN, BK = 64, S = N64_S, Threads = 256, WarpsN = BN / 64;
static_assert((BM / 64) * (BN / 64) == 8 && BM % 64 == 0 && BN % 64 == 0, "8 warps of 64x64");
constexpr int StageA = BM * BK, StageB = BN * BK, StageBytes = StageA + StageB;
constexpr int SharedBytes = S * StageBytes;
constexpr int GroupM = N64_GM;
constexpr int ScanTiles = 128, ScanStride = kTranscriptU32 + 1;
static_assert(ScanTiles * ScanStride * 4 <= SharedBytes, "scan scratch");

__device__ __forceinline__ void ldsm4(uint32_t (&r)[4], uint32_t addr) {
    asm volatile("ldmatrix.sync.aligned.m8n8.x4.shared.b16 {%0,%1,%2,%3}, [%4];\n"
                 : "=r"(r[0]), "=r"(r[1]), "=r"(r[2]), "=r"(r[3]) : "r"(addr));
}
__device__ __forceinline__ void mma(uint32_t (&d)[4], const uint32_t (&a)[4], uint32_t b0, uint32_t b1) {
    asm volatile("mma.sync.aligned.m16n8k32.row.col.s32.s8.s8.s32 "
                 "{%0,%1,%2,%3}, {%4,%5,%6,%7}, {%8,%9}, {%0,%1,%2,%3};\n"
                 : "+r"(d[0]), "+r"(d[1]), "+r"(d[2]), "+r"(d[3])
                 : "r"(a[0]), "r"(a[1]), "r"(a[2]), "r"(a[3]), "r"(b0), "r"(b1));
}
__device__ __forceinline__ void cpasync16(uint32_t dst, const void *src) {
    asm volatile("cp.async.cg.shared.global [%0], [%1], 16;\n" :: "r"(dst), "l"(src));
}
__device__ __forceinline__ void commit() { asm volatile("cp.async.commit_group;\n" ::); }
template <int N> __device__ __forceinline__ void waitPrior() { asm volatile("cp.async.wait_group %0;\n" :: "n"(N)); }
}  // namespace n64
namespace n64 {
__host__ inline bool shapeOk(int m, int n, int k, int rank) {
    return m > 0 && n > 0 && m % BM == 0 && n % BN == 0 && k == 8192 && rank == 128;
}
__host__ inline dim3 grid(int m, int n) { return dim3(unsigned(n / BN), unsigned(m / BM)); }
}  // namespace n64

#if defined(CANARY_N64_TMA_SCAN_OUTPUT)
// SM120 fused scan with the existing public scan-output contract.
#define N64_EPILOGUE_SCAN_OUTPUT 1
__global__ __launch_bounds__(256, 1) void noisyGemmN64Combined(
    const int8_t *__restrict__ A, const int8_t *__restrict__ Bn,
    int32_t *__restrict__ cNoised, int m, int n, int runtimeK, int runtimeRank, bool writeC,
    tma_delivery::ScanOutput scanOut) {
    uint32_t *const transcripts = nullptr; (void)transcripts;
#include "noisy_gemm_n64_body.inl"
}
#undef N64_EPILOGUE_SCAN_OUTPUT
#endif
}  // namespace pearl
}  // namespace om
