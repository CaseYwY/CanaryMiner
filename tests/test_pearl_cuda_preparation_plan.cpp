#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "../src/algos/pearl-pow/cuda_preparation_plan.h"

namespace {
using namespace om;
using namespace om::pearl;

int failures = 0;

void check(bool ok, const char *name) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

struct LegacyGolden {
    CudaPreparationPlanToken token;
    uint64_t planId;
    const char *name;
    const char *kernelId;
    const char *tileId;
    int blockM, blockN, threads;
    bool nMajorB;
    int architecture, blockK;
    TileFamily family;
    int wm, wn, tm, tn, stages, groupM;
};

// Frozen from the pre-extraction algo.cu catalog (SHA-256 8a376320...).
// Literal old scalar fields and launcher template arguments are intentionally
// independent of the descriptor's derived geometry.
constexpr LegacyGolden kLegacyGolden[] = {
    {CudaPreparationPlanToken::SingleWm2Wn4Tm2Tn2, UINT64_C(0x4355444100010001),
     "single 2x4/2x2", "cuda-wmma-single-v1", "wm2-wn4-tm2-tn2-bk32",
     64, 128, 256, false, 70, 32, kSingle,
     2, 4, 2, 2, 1, 0},
    {CudaPreparationPlanToken::SingleWm2Wn4Tm4Tn2, UINT64_C(0x4355444100010002),
     "single 2x4/4x2", "cuda-wmma-single-v1", "wm2-wn4-tm4-tn2-bk32",
     128, 128, 256, false, 70, 32, kSingle,
     2, 4, 4, 2, 1, 0},
    {CudaPreparationPlanToken::SingleWm2Wn4Tm2Tn4, UINT64_C(0x4355444100010003),
     "single 2x4/2x4", "cuda-wmma-single-v1", "wm2-wn4-tm2-tn4-bk32",
     64, 256, 256, false, 70, 32, kSingle,
     2, 4, 2, 4, 1, 0},
    {CudaPreparationPlanToken::SingleWm2Wn4Tm4Tn4, UINT64_C(0x4355444100010004),
     "single 2x4/4x4", "cuda-wmma-single-v1", "wm2-wn4-tm4-tn4-bk32",
     128, 256, 256, false, 70, 32, kSingle,
     2, 4, 4, 4, 1, 0},
    {CudaPreparationPlanToken::SingleWm4Wn2Tm2Tn2, UINT64_C(0x4355444100010005),
     "single 4x2/2x2", "cuda-wmma-single-v1", "wm4-wn2-tm2-tn2-bk32",
     128, 64, 256, false, 70, 32, kSingle,
     4, 2, 2, 2, 1, 0},
    {CudaPreparationPlanToken::SingleWm4Wn4Tm2Tn2, UINT64_C(0x4355444100010006),
     "single 4x4/2x2", "cuda-wmma-single-v1", "wm4-wn4-tm2-tn2-bk32",
     128, 128, 512, false, 70, 32, kSingle,
     4, 4, 2, 2, 1, 0},
    {CudaPreparationPlanToken::DoubleBufferedWm2Wn4Tm2Tn2, UINT64_C(0x4355444100010007),
     "dbuf 2x4/2x2", "cuda-wmma-dbuf-v1", "wm2-wn4-tm2-tn2-bk32",
     64, 128, 256, false, 70, 32, kDoubleBuffered,
     2, 4, 2, 2, 2, 0},
    {CudaPreparationPlanToken::DoubleBufferedWm2Wn4Tm4Tn2, UINT64_C(0x4355444100010008),
     "dbuf 2x4/4x2", "cuda-wmma-dbuf-v1", "wm2-wn4-tm4-tn2-bk32",
     128, 128, 256, false, 70, 32, kDoubleBuffered,
     2, 4, 4, 2, 2, 0},
    {CudaPreparationPlanToken::DoubleBufferedWm2Wn4Tm2Tn4, UINT64_C(0x4355444100010009),
     "dbuf 2x4/2x4", "cuda-wmma-dbuf-v1", "wm2-wn4-tm2-tn4-bk32",
     64, 256, 256, false, 70, 32, kDoubleBuffered,
     2, 4, 2, 4, 2, 0},
    {CudaPreparationPlanToken::DoubleBufferedWm2Wn4Tm4Tn4, UINT64_C(0x435544410001000a),
     "dbuf 2x4/4x4", "cuda-wmma-dbuf-v1", "wm2-wn4-tm4-tn4-bk32",
     128, 256, 256, false, 70, 32, kDoubleBuffered,
     2, 4, 4, 4, 2, 0},
    {CudaPreparationPlanToken::DoubleBufferedWm4Wn2Tm2Tn2, UINT64_C(0x435544410001000b),
     "dbuf 4x2/2x2", "cuda-wmma-dbuf-v1", "wm4-wn2-tm2-tn2-bk32",
     128, 64, 256, false, 70, 32, kDoubleBuffered,
     4, 2, 2, 2, 2, 0},
    {CudaPreparationPlanToken::DoubleBufferedWm4Wn4Tm2Tn2, UINT64_C(0x435544410001000c),
     "dbuf 4x4/2x2", "cuda-wmma-dbuf-v1", "wm4-wn4-tm2-tn2-bk32",
     128, 128, 512, false, 70, 32, kDoubleBuffered,
     4, 4, 2, 2, 2, 0},
    {CudaPreparationPlanToken::AsyncWm2Wn4Tm2Tn2, UINT64_C(0x435544410001000d),
     "async 2x4/2x2", "cuda-wmma-async-v1", "wm2-wn4-tm2-tn2-bk32",
     64, 128, 256, false, 80, 32, kAsync,
     2, 4, 2, 2, 4, 0},
    {CudaPreparationPlanToken::AsyncWm2Wn4Tm4Tn2, UINT64_C(0x435544410001000e),
     "async 2x4/4x2", "cuda-wmma-async-v1", "wm2-wn4-tm4-tn2-bk32",
     128, 128, 256, false, 80, 32, kAsync,
     2, 4, 4, 2, 4, 0},
    {CudaPreparationPlanToken::AsyncWm2Wn4Tm2Tn4, UINT64_C(0x435544410001000f),
     "async 2x4/2x4", "cuda-wmma-async-v1", "wm2-wn4-tm2-tn4-bk32",
     64, 256, 256, false, 80, 32, kAsync,
     2, 4, 2, 4, 4, 0},
    {CudaPreparationPlanToken::AsyncWm2Wn4Tm4Tn4, UINT64_C(0x4355444100010010),
     "async 2x4/4x4", "cuda-wmma-async-v1", "wm2-wn4-tm4-tn4-bk32",
     128, 256, 256, false, 80, 32, kAsync,
     2, 4, 4, 4, 4, 0},
    {CudaPreparationPlanToken::AsyncWm4Wn2Tm2Tn2, UINT64_C(0x4355444100010011),
     "async 4x2/2x2", "cuda-wmma-async-v1", "wm4-wn2-tm2-tn2-bk32",
     128, 64, 256, false, 80, 32, kAsync,
     4, 2, 2, 2, 4, 0},
    {CudaPreparationPlanToken::AsyncWm4Wn4Tm2Tn2, UINT64_C(0x4355444100010012),
     "async 4x4/2x2", "cuda-wmma-async-v1", "wm4-wn4-tm2-tn2-bk32",
     128, 128, 512, false, 80, 32, kAsync,
     4, 4, 2, 2, 4, 0},
    {CudaPreparationPlanToken::PtxBk64Wm2Wn4Tm2Tn2, UINT64_C(0x4355444100010013),
     "ptx k64 2x4/2x2", "cuda-ptx-mma-s8-v1", "wm2-wn4-tm2-tn2-bk64-st3-gm8",
     64, 128, 256, true, 80, 64, kPtx,
     2, 4, 2, 2, 3, 8},
    {CudaPreparationPlanToken::PtxBk64Wm2Wn4Tm4Tn2, UINT64_C(0x4355444100010014),
     "ptx k64 2x4/4x2", "cuda-ptx-mma-s8-v1", "wm2-wn4-tm4-tn2-bk64-st3-gm8",
     128, 128, 256, true, 80, 64, kPtx,
     2, 4, 4, 2, 3, 8},
    {CudaPreparationPlanToken::PtxBk64Wm2Wn4Tm2Tn4, UINT64_C(0x4355444100010015),
     "ptx k64 2x4/2x4", "cuda-ptx-mma-s8-v1", "wm2-wn4-tm2-tn4-bk64-st3-gm8",
     64, 256, 256, true, 80, 64, kPtx,
     2, 4, 2, 4, 3, 8},
    {CudaPreparationPlanToken::PtxBk64Wm2Wn4Tm4Tn4, UINT64_C(0x4355444100010016),
     "ptx k64 2x4/4x4", "cuda-ptx-mma-s8-v1", "wm2-wn4-tm4-tn4-bk64-st3-gm8",
     128, 256, 256, true, 80, 64, kPtx,
     2, 4, 4, 4, 3, 8},
    {CudaPreparationPlanToken::PtxBk64Wm4Wn2Tm2Tn2, UINT64_C(0x4355444100010017),
     "ptx k64 4x2/2x2", "cuda-ptx-mma-s8-v1", "wm4-wn2-tm2-tn2-bk64-st3-gm8",
     128, 64, 256, true, 80, 64, kPtx,
     4, 2, 2, 2, 3, 8},
    {CudaPreparationPlanToken::PtxBk64Wm4Wn4Tm2Tn2, UINT64_C(0x4355444100010018),
     "ptx k64 4x4/2x2", "cuda-ptx-mma-s8-v1", "wm4-wn4-tm2-tn2-bk64-st3-gm8",
     128, 128, 512, true, 80, 64, kPtx,
     4, 4, 2, 2, 3, 8},
    {CudaPreparationPlanToken::PtxBk64Wm4Wn4Tm4Tn2, UINT64_C(0x4355444100010019),
     "ptx k64 4x4/4x2", "cuda-ptx-mma-s8-v1", "wm4-wn4-tm4-tn2-bk64-st3-gm8",
     256, 128, 512, true, 80, 64, kPtx,
     4, 4, 4, 2, 3, 8},
    {CudaPreparationPlanToken::PtxBk64Wm4Wn4Tm2Tn4, UINT64_C(0x435544410001001a),
     "ptx k64 4x4/2x4", "cuda-ptx-mma-s8-v1", "wm4-wn4-tm2-tn4-bk64-st3-gm8",
     128, 256, 512, true, 80, 64, kPtx,
     4, 4, 2, 4, 3, 8},
    {CudaPreparationPlanToken::PtxBk64Wm8Wn2Tm2Tn2, UINT64_C(0x435544410001001b),
     "ptx k64 8x2/2x2", "cuda-ptx-mma-s8-v1", "wm8-wn2-tm2-tn2-bk64-st3-gm8",
     256, 64, 512, true, 80, 64, kPtx,
     8, 2, 2, 2, 3, 8},
    {CudaPreparationPlanToken::PtxBk64Wm4Wn8Tm2Tn2, UINT64_C(0x435544410001001c),
     "ptx k64 4x8/2x2", "cuda-ptx-mma-s8-v1", "wm4-wn8-tm2-tn2-bk64-st3-gm8",
     128, 256, 1024, true, 80, 64, kPtx,
     4, 8, 2, 2, 3, 8},
    {CudaPreparationPlanToken::PtxBk32Wm2Wn4Tm2Tn2, UINT64_C(0x435544410001001d),
     "ptx k32 2x4/2x2", "cuda-ptx-mma-s8-v1", "wm2-wn4-tm2-tn2-bk32-st3-gm8",
     64, 128, 256, true, 80, 32, kPtx,
     2, 4, 2, 2, 3, 8},
    {CudaPreparationPlanToken::PtxBk32Wm2Wn4Tm4Tn2, UINT64_C(0x435544410001001e),
     "ptx k32 2x4/4x2", "cuda-ptx-mma-s8-v1", "wm2-wn4-tm4-tn2-bk32-st3-gm8",
     128, 128, 256, true, 80, 32, kPtx,
     2, 4, 4, 2, 3, 8},
    {CudaPreparationPlanToken::PtxBk32Wm2Wn4Tm2Tn4, UINT64_C(0x435544410001001f),
     "ptx k32 2x4/2x4", "cuda-ptx-mma-s8-v1", "wm2-wn4-tm2-tn4-bk32-st3-gm8",
     64, 256, 256, true, 80, 32, kPtx,
     2, 4, 2, 4, 3, 8},
    {CudaPreparationPlanToken::PtxBk32Wm2Wn4Tm4Tn4, UINT64_C(0x4355444100010020),
     "ptx k32 2x4/4x4", "cuda-ptx-mma-s8-v1", "wm2-wn4-tm4-tn4-bk32-st3-gm8",
     128, 256, 256, true, 80, 32, kPtx,
     2, 4, 4, 4, 3, 8},
    {CudaPreparationPlanToken::PtxBk32Wm4Wn2Tm2Tn2, UINT64_C(0x4355444100010021),
     "ptx k32 4x2/2x2", "cuda-ptx-mma-s8-v1", "wm4-wn2-tm2-tn2-bk32-st3-gm8",
     128, 64, 256, true, 80, 32, kPtx,
     4, 2, 2, 2, 3, 8},
    {CudaPreparationPlanToken::PtxBk32Wm4Wn4Tm2Tn2, UINT64_C(0x4355444100010022),
     "ptx k32 4x4/2x2", "cuda-ptx-mma-s8-v1", "wm4-wn4-tm2-tn2-bk32-st3-gm8",
     128, 128, 512, true, 80, 32, kPtx,
     4, 4, 2, 2, 3, 8},
    {CudaPreparationPlanToken::PtxBk32Wm4Wn4Tm4Tn2, UINT64_C(0x4355444100010023),
     "ptx k32 4x4/4x2", "cuda-ptx-mma-s8-v1", "wm4-wn4-tm4-tn2-bk32-st3-gm8",
     256, 128, 512, true, 80, 32, kPtx,
     4, 4, 4, 2, 3, 8},
    {CudaPreparationPlanToken::PtxBk32Wm4Wn4Tm2Tn4, UINT64_C(0x4355444100010024),
     "ptx k32 4x4/2x4", "cuda-ptx-mma-s8-v1", "wm4-wn4-tm2-tn4-bk32-st3-gm8",
     128, 256, 512, true, 80, 32, kPtx,
     4, 4, 2, 4, 3, 8},
    {CudaPreparationPlanToken::PtxBk32Wm8Wn2Tm2Tn2, UINT64_C(0x4355444100010025),
     "ptx k32 8x2/2x2", "cuda-ptx-mma-s8-v1", "wm8-wn2-tm2-tn2-bk32-st3-gm8",
     256, 64, 512, true, 80, 32, kPtx,
     8, 2, 2, 2, 3, 8},
    {CudaPreparationPlanToken::PtxBk32Wm4Wn8Tm2Tn2, UINT64_C(0x4355444100010026),
     "ptx k32 4x8/2x2", "cuda-ptx-mma-s8-v1", "wm4-wn8-tm2-tn2-bk32-st3-gm8",
     128, 256, 1024, true, 80, 32, kPtx,
     4, 8, 2, 2, 3, 8},
};

bool sameRequest(const PreparedJobRequest &a, const PreparedJobRequest &b) {
    return preparedConfigEqual(a.config, b.config) && a.m == b.m &&
           a.n == b.n && a.preparationPlanId == b.preparationPlanId &&
           a.mode == b.mode;
}

PreparedJobRequest sentinelRequest() {
    PreparedJobRequest request;
    request.config = MiningConfig{4096, 256, 8, 8};
    request.m = 17;
    request.n = 29;
    request.preparationPlanId = UINT64_C(0xfedcba9876543210);
    request.mode = PreparedJobMode::SyntheticHardTarget;
    return request;
}

void expectFailure(const CudaPreparationPlanDescriptor &plan, uint32_t m,
                   uint32_t n, int architecture, PreparedJobMode mode,
                   CudaPreparationPlanError expected) {
    PreparedJobRequest out = sentinelRequest();
    const PreparedJobRequest before = out;
    CudaPreparationPlanError error = CudaPreparationPlanError::None;
    check(!buildCudaPreparedJobRequest(plan, m, n, architecture, mode, &out,
                                      &error),
          "invalid request is refused");
    check(error == expected, "request failure reports exact error");
    check(sameRequest(out, before), "request failure preserves output");
}

void catalogGoldenTest() {
    static_assert(kCudaPreparationPlanCount == 38, "all legacy rows retained");
    static_assert(sizeof(kLegacyGolden) / sizeof(kLegacyGolden[0]) == 38,
                  "golden covers every row");
    static_assert(cudaPreparationPlanCatalogValid(), "catalog valid");
    static_assert(noexcept(buildCudaPreparedJobRequest(
                      kCudaPreparationPlans[0], 4096, 16384, 80,
                      PreparedJobMode::Normal, nullptr)),
                  "request builder is no-throw");
    static_assert(std::is_trivially_copyable<PreparedJobRequest>::value,
                  "request publication remains fixed-size");

    for (size_t i = 0; i < kCudaPreparationPlanCount; ++i) {
        const auto &p = kCudaPreparationPlans[i];
        const auto &g = kLegacyGolden[i];
        check(p.token == g.token && p.preparationPlanId == g.planId &&
                  cudaPreparationTextEqual(p.name, g.name) &&
                  cudaPreparationTextEqual(p.kernelId, g.kernelId) &&
                  cudaPreparationTextEqual(p.tileId, g.tileId) &&
                  p.blockM == g.blockM && p.blockN == g.blockN &&
                  p.threads == g.threads && p.nMajorB == g.nMajorB &&
                  p.architecture.capability == g.architecture &&
                  p.architecture.match == TileArchMatch::AtLeast &&
                  p.blockK == g.blockK && p.family == g.family &&
                  p.wm == g.wm && p.wn == g.wn && p.tm == g.tm &&
                  p.tn == g.tn && p.stages == g.stages &&
                  p.groupM == g.groupM,
              "all 38 old rows/order/scalars/launcher arguments are exact");
        check(findCudaPreparationPlan(p.token) == &p &&
                  findCudaPreparationPlan(p.kernelId, p.tileId) == &p,
              "both lookups return the canonical descriptor");
        check((p.preparationPlanId >> 32) == UINT64_C(0x43554441) &&
                  ((p.preparationPlanId >> 16) & 0xffffu) == 1 &&
                  (p.preparationPlanId & 0xffffu) != 0,
              "stable ID uses explicit CUDA namespace/ABI/nonzero token");
    }
    check(findCudaPreparationPlan(static_cast<CudaPreparationPlanToken>(0)) ==
              nullptr &&
              findCudaPreparationPlan(
                  static_cast<CudaPreparationPlanToken>(0xffff)) == nullptr,
          "unknown tokens are refused");
    check(findCudaPreparationPlan("cuda-wmma-single-v1", "future-tile") ==
              nullptr &&
              findCudaPreparationPlan("future-kernel",
                                      "wm2-wn4-tm2-tn2-bk32") == nullptr,
          "unknown exact ID pairs are refused");
    std::string extended(kCudaPreparationPlans[0].kernelId);
    extended.append("\0tail", 5);
    check(findCudaPreparationPlan(
              std::string_view(extended.data(), extended.size()),
              kCudaPreparationPlans[0].tileId) == nullptr,
          "ID-pair lookup does not truncate a supplied string_view at NUL");

    std::vector<CudaPreparationPlanDescriptor> reordered(
        std::begin(kCudaPreparationPlans), std::end(kCudaPreparationPlans));
    std::reverse(reordered.begin(), reordered.end());
    for (const auto &p : reordered) {
        PreparedJobRequest out;
        check(buildCudaPreparedJobRequest(p, 4096, 16384, 120,
                                          PreparedJobMode::Normal, &out) &&
                  out.preparationPlanId == p.preparationPlanId,
              "reordering or filtering cannot renumber plan identity");
    }
}

void requestValidationTest() {
    constexpr CudaPreparationShapeDescriptor shapes[] = {
        {"m4096-n65536",4096,65536}, {"m8192-n16384",8192,16384},
        {"m4096-n32768",4096,32768}, {"m4096-n16384",4096,16384},
        {"m2048-n65536",2048,65536}, {"m2048-n32768",2048,32768},
        {"m2048-n16384",2048,16384}, {"m2048-n8192",2048,8192},
        {"m1024-n32768",1024,32768}, {"m1024-n16384",1024,16384},
        {"m512-n8192",512,8192}, {"m256-n2048",256,2048},
    };
    static_assert(kCudaPreparationShapeCount == 12,
                  "all legacy shape rows retained");
    for (size_t i = 0; i < kCudaPreparationShapeCount; ++i) {
        const auto &current = kCudaPreparationShapes[i];
        check(cudaPreparationTextEqual(current.id, shapes[i].id) &&
                  current.m == shapes[i].m && current.n == shapes[i].n &&
                  findCudaPreparationShape(current.m, current.n) == &current,
              "all 12 legacy shape rows/order/IDs/dimensions are exact");
    }
    for (const auto &p : kCudaPreparationPlans) {
        for (const auto &shape : shapes) {
            PreparedJobRequest out = sentinelRequest();
            CudaPreparationPlanError error =
                CudaPreparationPlanError::MetadataMismatch;
            check(buildCudaPreparedJobRequest(
                      p, shape.m, shape.n, p.architecture.capability,
                      PreparedJobMode::Normal, &out, &error),
                  "every legacy plan/shape combination builds");
            check(error == CudaPreparationPlanError::None &&
                      out.config.commonDim == 2048 && out.config.rank == 128 &&
                      out.config.tileH == 16 && out.config.tileW == 16 &&
                      out.m == shape.m && out.n == shape.n &&
                      out.preparationPlanId == p.preparationPlanId &&
                      out.mode == PreparedJobMode::Normal,
                  "builder publishes only exact fixed CUDA config/shape/plan");
        }
        expectFailure(p, 4096, 16384, p.architecture.capability - 1,
                      PreparedJobMode::Normal,
                      CudaPreparationPlanError::InvalidArchitecture);
        expectFailure(p, 4096, 16384, 0, PreparedJobMode::Normal,
                      CudaPreparationPlanError::InvalidArchitecture);
        expectFailure(p, 4096, 16384, -1, PreparedJobMode::Normal,
                      CudaPreparationPlanError::InvalidArchitecture);
        PreparedJobRequest hard;
        check(buildCudaPreparedJobRequest(p, 4096, 16384, 120,
                                          PreparedJobMode::SyntheticHardTarget,
                                          &hard) &&
                  hard.mode == PreparedJobMode::SyntheticHardTarget,
              "known synthetic mode is retained for later context validation");
    }
    const auto &p = kCudaPreparationPlans[0];
    CudaPreparationPlanError error = CudaPreparationPlanError::None;
    check(!buildCudaPreparedJobRequest(p, 4096, 16384, 80,
                                      PreparedJobMode::Normal, nullptr, &error) &&
              error == CudaPreparationPlanError::NullOutput,
          "null output is refused");
    expectFailure(p, 0, 16384, 80, PreparedJobMode::Normal,
                  CudaPreparationPlanError::InvalidShape);
    expectFailure(p, 4096, 0, 80, PreparedJobMode::Normal,
                  CudaPreparationPlanError::InvalidShape);
    expectFailure(p, 192, 16384, 80, PreparedJobMode::Normal,
                  CudaPreparationPlanError::InvalidShape);
    expectFailure(p, 4096, 192, 80, PreparedJobMode::Normal,
                  CudaPreparationPlanError::InvalidShape);
    expectFailure(p, 64, 16384, 80, PreparedJobMode::Normal,
                  CudaPreparationPlanError::InvalidShape);
    expectFailure(p, 4096, 64, 80, PreparedJobMode::Normal,
                  CudaPreparationPlanError::InvalidShape);
    expectFailure(p, 256, 4096, 80, PreparedJobMode::Normal,
                  CudaPreparationPlanError::InvalidShape);
    expectFailure(p, 4096, 1u << 24, 80, PreparedJobMode::Normal,
                  CudaPreparationPlanError::InvalidShape);
    expectFailure(p, (1u << 24) + 1, 16384, 80, PreparedJobMode::Normal,
                  CudaPreparationPlanError::InvalidShape);
    expectFailure(p, 4096, (1u << 24) + 1, 80, PreparedJobMode::Normal,
                  CudaPreparationPlanError::InvalidShape);
    expectFailure(p, 1u << 24, 2048, 80, PreparedJobMode::Normal,
                  CudaPreparationPlanError::InvalidShape);
    expectFailure(p, UINT32_MAX, UINT32_MAX, 80, PreparedJobMode::Normal,
                  CudaPreparationPlanError::InvalidShape);
    const auto &wide =
        cudaPreparationPlan<CudaPreparationPlanToken::PtxBk64Wm4Wn4Tm4Tn2>();
    expectFailure(wide, 128, 2048, 80, PreparedJobMode::Normal,
                  CudaPreparationPlanError::InvalidShape);
    expectFailure(wide, 1u << 22, 1u << 24, 80, PreparedJobMode::Normal,
                  CudaPreparationPlanError::InvalidShape);
    expectFailure(p, 4096, 16384, 80, static_cast<PreparedJobMode>(0),
                  CudaPreparationPlanError::InvalidMode);
}

void metadataMismatchTest() {
    const auto &base = kCudaPreparationPlans[0];
    auto reject = [&](const CudaPreparationPlanDescriptor &changed) {
        expectFailure(changed, 4096, 16384, 80, PreparedJobMode::Normal,
                      CudaPreparationPlanError::MetadataMismatch);
    };
    auto p = base;
    p.token = static_cast<CudaPreparationPlanToken>(0);
    expectFailure(p, 4096, 16384, 80, PreparedJobMode::Normal,
                  CudaPreparationPlanError::UnknownPlan);
    p = base; p.token = kCudaPreparationPlans[1].token; reject(p);
    p = base; p.preparationPlanId ^= 1; reject(p);
    p = base; p.name = "changed"; reject(p);
    p = base; p.name = nullptr; reject(p);
    p = base; p.kernelId = "changed"; reject(p);
    p = base; p.tileId = "changed"; reject(p);
    p = base; p.family = kPtx; reject(p);
    p = base; ++p.wm; reject(p);
    p = base; ++p.wn; reject(p);
    p = base; ++p.tm; reject(p);
    p = base; ++p.tn; reject(p);
    p = base; ++p.blockM; reject(p);
    p = base; ++p.blockN; reject(p);
    p = base; ++p.threads; reject(p);
    p = base; ++p.blockK; reject(p);
    p = base; ++p.stages; reject(p);
    p = base; ++p.groupM; reject(p);
    p = base; p.nMajorB = true; reject(p);
    p = base; ++p.architecture.capability; reject(p);
    p = base; p.architecture.match = TileArchMatch::Exact; reject(p);

    struct Containing {
        CudaPreparationPlanDescriptor selected;
        PreparedJobRequest request;
    } containing{base, sentinelRequest()};
    const auto before = containing.selected;
    check(buildCudaPreparedJobRequest(
              containing.selected, 4096, 16384, 80, PreparedJobMode::Normal,
              &containing.request) &&
              cudaPreparationPlanDescriptorEqual(containing.selected, before),
          "contained input/output remain independent and input is unchanged");
}

Job canonicalJob(PearlNetwork network, uint64_t height,
                 PearlCertificateVersion certificate,
                 PearlTargetSemantics semantics, const std::string &id,
                 const std::array<uint64_t,4> &target) {
    Job job{};
    PearlJobContext context;
    context.network = network;
    context.height = height;
    context.certificateVersion = certificate;
    context.targetSemantics = semantics;
    context.transportJobId = id;
    if (network == PearlNetwork::SyntheticBenchmark) {
        uint8_t header[kPearlBenchmarkHeaderBytes];
        fillPearlBenchmarkHeader(header);
        std::memcpy(context.header.data(), header, sizeof(header));
    } else
        for (size_t i = 0; i < context.header.size(); ++i)
            context.header[i] = static_cast<uint8_t>(i * 17u + 3u);
    check(encodePearlJobContext(context, &job.extra, nullptr),
          "canonical fixture encodes");
    b3::hash(nullptr, context.header.data(), context.header.size(), job.msg);
    std::memcpy(job.target, target.data(), sizeof(job.target));
    job.epoch = height;
    job.jobId = id;
    job.valid = true;
    return job;
}

void canonicalBuilderBridgeTest() {
    const auto &plan = kCudaPreparationPlans[0];
    PreparedJobRequest request;
    check(buildCudaPreparedJobRequest(plan, 4096, 16384, 80,
                                      PreparedJobMode::Normal, &request),
          "canonical bridge request builds");
    const PearlSourcePolicy mainPolicy{
        PearlNetwork::Mainnet, PearlTargetSemantics::BaseBeforePenalty};
    const std::string binaryId("job\0:binary", 11);
    Job job = canonicalJob(PearlNetwork::Mainnet, kPearlMainnetV3Activation,
                           PearlCertificateVersion::V3,
                           PearlTargetSemantics::BaseBeforePenalty,
                           binaryId, {5,0,0,0});
    PreparedJobHost prepared;
    check(buildPreparedJobHost(job, &mainPolicy, request, &prepared),
          "registry request feeds canonical builder");
    U256 expected;
    check(request.config.penalizedTarget(U256::fromLimbs(job.target), &expected) &&
              preparedU256Equal(prepared.resolvedBound, expected),
          "canonical resolution applies penalty exactly once");
    uint8_t bytes[32];
    expected.toBytesLE(bytes);
    check(std::memcmp(bytes, prepared.targetBytesLE.data(), sizeof(bytes)) == 0,
          "resolved bound is the exact device upload bytes");

    Job differentId = canonicalJob(
        PearlNetwork::Mainnet, kPearlMainnetV3Activation,
        PearlCertificateVersion::V3, PearlTargetSemantics::BaseBeforePenalty,
        std::string("job\0:other!", 11), {5,0,0,0});
    PreparedJobHost idPrepared;
    check(buildPreparedJobHost(differentId, &mainPolicy, request, &idPrepared) &&
              idPrepared == prepared,
          "binary transport-ID-only change reuses computational preparation");
    differentId.jobId = "mismatch";
    check(!buildPreparedJobHost(differentId, &mainPolicy, request, &idPrepared),
          "mismatched transport identity cannot enter preparation");

    for (const auto &other : kCudaPreparationPlans) {
        PreparedJobRequest otherRequest;
        PreparedJobHost otherPrepared;
        check(buildCudaPreparedJobRequest(other, 4096, 16384, 80,
                                          PreparedJobMode::Normal,
                                          &otherRequest) &&
                  buildPreparedJobHost(job, &mainPolicy, otherRequest,
                                       &otherPrepared),
              "every descriptor bridges to canonical preparation");
        check((otherPrepared == prepared) == (other.token == plan.token),
              "every distinct stable plan token changes computational identity");
    }

    Job prePenalty = canonicalJob(
        PearlNetwork::Mainnet, kPearlMainnetRankPenaltyActivation - 1,
        PearlCertificateVersion::V2, PearlTargetSemantics::BaseBeforePenalty,
        "pre", {5,0,0,0});
    check(buildPreparedJobHost(prePenalty, &mainPolicy, request, &prepared) &&
              prepared.resolvedBound.v[0] == 5 &&
              prepared.resolvedBound.v[1] == 0,
          "request bridge does not introduce pre-fork penalty");
    const PearlSourcePolicy finalPolicy{
        PearlNetwork::Mainnet, PearlTargetSemantics::FinalAdjustedBound};
    Job final = canonicalJob(
        PearlNetwork::Mainnet, kPearlMainnetV3Activation,
        PearlCertificateVersion::V3, PearlTargetSemantics::FinalAdjustedBound,
        "final", {UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX});
    check(buildPreparedJobHost(final, &finalPolicy, request, &prepared) &&
              prepared.resolvedBound.v[3] == UINT64_MAX,
          "final-adjusted maximum bound is not penalized again");
    for (auto &limb : job.target) limb = UINT64_MAX;
    check(!buildPreparedJobHost(job, &mainPolicy, request, &prepared),
          "overflow remains a canonical rejection");

    PreparedJobRequest hardRequest;
    check(buildCudaPreparedJobRequest(plan, 4096, 16384, 80,
                                      PreparedJobMode::SyntheticHardTarget,
                                      &hardRequest),
          "synthetic hard request builds without inventing a source context");
    check(!buildPreparedJobHost(final, &finalPolicy, hardRequest, &prepared),
          "request mode cannot bypass canonical public hard-mode rejection");
    Job synthetic = canonicalJob(
        PearlNetwork::SyntheticBenchmark, kPearlBenchmarkEpoch,
        PearlCertificateVersion::V1, PearlTargetSemantics::BaseBeforePenalty,
        "", {0,0,0,0});
    const PearlSourcePolicy syntheticPolicy{
        PearlNetwork::SyntheticBenchmark,
        PearlTargetSemantics::BaseBeforePenalty};
    check(buildPreparedJobHost(synthetic, &syntheticPolicy, hardRequest,
                               &prepared) &&
              prepared.resolvedBound.v[0] == 0 &&
              prepared.resolvedBound.v[3] == 0,
          "canonical synthetic hard target remains zero");
}

void fixedExperimentTest() {
    using Selection = CudaFixedExperimentSelection;
    const auto &plan = kCudaFixedExperimentPlan;
    check(kCudaPreparationPlanCount == 38 && cudaPreparationPlanCatalogValid(),
          "experimental extension leaves default38 catalog intact");
    check(plan.preparationPlanId == UINT64_C(0x4355444100010027) &&
          plan.token == CudaPreparationPlanToken::ExperimentalSm120FixedK2048R128 &&
          std::string_view(plan.kernelId) == "cuda-ptx-mma-s8-sm120-fixed-k2048-r128-experimental-v1",
          "experimental identity is exact and permanent");
    check(findCudaPreparationPlan(plan.token) == &plan &&
          findCudaPreparationPlan(plan.kernelId, plan.tileId) == &plan,
          "experimental canonical lookup retains distinct identity");
    for (const auto &normal : kCudaPreparationPlans) {
        check(normal.token != plan.token && normal.preparationPlanId != plan.preparationPlanId &&
              std::string_view(normal.kernelId) != plan.kernelId, "experiment never aliases default identity");
    }
    check(plan.family == kPtx && plan.blockM == 128 && plan.blockN == 256 &&
          plan.threads == 512 && plan.blockK == 64 && plan.stages == 3 && plan.groupM == 8 &&
          plan.architecture.match == TileArchMatch::Exact, "exact experimental layout and PTX family");
    check(parseCudaFixedExperiment(nullptr) == Selection::Disabled, "absent opt-in disables");
    check(parseCudaFixedExperiment(kCudaFixedExperimentOptIn) == Selection::Enabled, "exact opt-in enables");
    for (const char *text : {"", "0", "1", "true", "sm120-fixed-k2048-r128-v1 ",
                             " sm120-fixed-k2048-r128-v1", "SM120-FIXED-K2048-R128-V1"})
        check(parseCudaFixedExperiment(text) == Selection::Invalid, "malformed opt-in fails");
    check(cudaFixedExperimentConfigurationValid(Selection::Disabled, false, true, true,
                                                "public.example", -1, true, true),
          "disabled extension imposes no policy on existing default modes");
    check(cudaFixedExperimentConfigurationValid(Selection::Enabled, true, false, false,
                                                "127.0.0.1", 1200, false, false),
          "isolated explicitly compiled loopback policy admitted");
    for (bool compiled : {false, true})
        for (bool formal : {false, true})
            for (bool synthetic : {false, true})
                check(cudaFixedExperimentConfigurationValid(Selection::Enabled, compiled, formal,
                      synthetic, "127.0.0.1", 1200, false, false) == (compiled && !formal && !synthetic),
                      "build/formal/synthetic policy is fail closed");
    check(!cudaFixedExperimentConfigurationValid(Selection::Invalid, true, false, false,
                                                 "127.0.0.1", 1200, false, false), "invalid selection refused");
    for (const char *host : {"", "localhost", "::1", "127.0.0.2", "pool.example", "127.0.0.1.evil"})
        check(!cudaFixedExperimentConfigurationValid(Selection::Enabled, true, false, false,
                                                     host, 1200, false, false), "literal loopback only");
    for (int port : {-1, 0, 65536})
        check(!cudaFixedExperimentConfigurationValid(Selection::Enabled, true, false, false,
                                                     "127.0.0.1", port, false, false), "port bound enforced");
    for (const auto flags : {std::array<bool, 2>{true, false}, {false, true}, {true, true}})
        check(!cudaFixedExperimentConfigurationValid(Selection::Enabled, true, false, false,
                    "127.0.0.1", 1200, flags[0], flags[1]), "conflicting shape/tile override refused");
    for (bool formal : {false, true}) {
        check(!cudaFixedExperimentVisits(Selection::Disabled, true, formal), "default never visits experimental witness");
        check(cudaFixedExperimentVisits(Selection::Enabled, true, formal) == !formal, "formal cannot visit extension");
        check(cudaFixedExperimentVisits(Selection::Enabled, false, formal), "old reference witnesses retained");
    }
    check(!cudaFixedExperimentSelectable(Selection::Enabled, false) &&
          cudaFixedExperimentSelectable(Selection::Enabled, true) &&
          cudaFixedExperimentSelectable(Selection::Disabled, false) &&
          !cudaFixedExperimentSelectable(Selection::Disabled, true), "no baseline fallback or default experimental selection");
    for (int arch : {0, 80, 89, 90, 100, 119, 120, 121, 130}) {
        check(tileArchitectureEligible(plan.architecture, arch) == (arch == 120), "exact SM120 eligibility");
        check(cudaFixedExperimentLaunchValid(arch, 4096, 65536, 2048, 128) == (arch == 120),
              "actual host launch architecture admission");
    }
    check(cudaFixedExperimentLaunchValid(120, 256, 256, 2048, 128), "unchanged CPU reference selfCheck admitted");
    for (const auto dims : {std::array<int, 4>{4096,65536,1024,128}, {4096,65536,2048,64},
                           {4095,65536,2048,128}, {4096,65535,2048,128}, {256,2048,2048,128},
                           {0,65536,2048,128}, {-4096,65536,2048,128}, {4096,16777216,2048,128}})
        check(!cudaFixedExperimentLaunchValid(120, dims[0], dims[1], dims[2], dims[3]), "invalid launch dimensions rejected");
    PreparedJobRequest request;
    check(buildCudaPreparedJobRequest(plan, 4096, 65536, 120, PreparedJobMode::Normal, &request) &&
          request.preparationPlanId == plan.preparationPlanId && request.config.commonDim == 2048 &&
          request.config.rank == 128, "canonical request binds experimental plan");
    const auto original = request;
    check(!buildCudaPreparedJobRequest(plan, 4096, 65536, 121, PreparedJobMode::Normal, &request) &&
          sameRequest(request, original), "wrong SM leaves output unchanged");
    check(!buildCudaPreparedJobRequest(plan, 4096, 16384, 120, PreparedJobMode::Normal, &request) &&
          sameRequest(request, original), "unproven source shape leaves output unchanged");
    check(!buildCudaPreparedJobRequest(plan, 4096, 65536, 120, PreparedJobMode::SyntheticHardTarget, &request) &&
          sameRequest(request, original), "synthetic experiment request refused transactionally");
    auto altered = plan;
    altered.preparationPlanId = kCudaPreparationPlans[25].preparationPlanId;
    check(!buildCudaPreparedJobRequest(altered, 4096, 65536, 120, PreparedJobMode::Normal, &request) &&
          sameRequest(request, original), "baseline ID substitution rejected transactionally");
}

void wm8ExperimentTest() {
    using Selection = CudaFixedExperimentSelection;
    const auto &plan = kCudaWm8ExperimentPlan;
    check(kCudaPreparationPlanCount == 38 && cudaPreparationPlanCatalogValid(), "WM8 does not change default catalog");
    check(plan.token == CudaPreparationPlanToken::ExperimentalSm120Wm8Runtime &&
          plan.preparationPlanId == UINT64_C(0x4355444100010028) &&
          std::string_view(plan.kernelId) == "cuda-ptx-mma-s8-sm120-wm8-runtime-experimental-v1" &&
          std::string_view(plan.tileId) == "wm8-wn2-tm2-tn4-bk64-st3-gm8",
          "WM8 permanent exact identity");
    check(findCudaPreparationPlan(plan.token) == &plan &&
          findCudaPreparationPlan(plan.kernelId, plan.tileId) == &plan,
          "WM8 canonical lookup");
    check(plan.wm == 8 && plan.wn == 2 && plan.tm == 2 && plan.tn == 4 &&
          plan.blockM == 256 && plan.blockN == 128 && plan.threads == 512 &&
          plan.blockK == 64 && plan.stages == 3 && plan.groupM == 8 && plan.nMajorB && plan.family == kPtx,
          "WM8 exact runtime launch geometry");
    check(parseCudaFixedExperiment(kCudaWm8ExperimentOptIn) == Selection::Wm8Runtime &&
          parseCudaFixedExperiment(kCudaFixedExperimentOptIn) == Selection::Enabled,
          "selectors cannot alias fixed and runtime experiments");
    for (const char *value : {"sm120-wm8-wn2-tm2-tn4-runtime-v1 ", "wm8", "SM120-WM8", "sm120-direct-fold-v1"})
        check(parseCudaFixedExperiment(value) == Selection::Invalid, "unregistered WM8 or direct selector rejected");
    for (bool fixed : {false, true}) for (bool wm8 : {false, true}) {
        check(cudaIsolatedExperimentCompiled(Selection::Enabled, fixed, wm8) == fixed &&
              cudaIsolatedExperimentCompiled(Selection::Wm8Runtime, fixed, wm8) == wm8 &&
              !cudaIsolatedExperimentCompiled(Selection::Invalid, fixed, wm8) &&
              !cudaIsolatedExperimentCompiled(Selection::Disabled, fixed, wm8), "independent compile capability matrix");
    }
    check(cudaFixedExperimentConfigurationValid(Selection::Wm8Runtime, true, false, false,
          "127.0.0.1", 1200, false, false), "WM8 exact loopback admission");
    for (bool compiled : {false, true}) for (bool formal : {false, true}) for (bool bench : {false, true})
        check(cudaFixedExperimentConfigurationValid(Selection::Wm8Runtime, compiled, formal, bench,
              "127.0.0.1", 1200, false, false) == (compiled && !formal && !bench), "WM8 configuration gate");
    check(!cudaFixedExperimentConfigurationValid(Selection::Wm8Runtime, true, false, false,
          "public.example", 1200, false, false) &&
          !cudaFixedExperimentConfigurationValid(Selection::Wm8Runtime, true, false, false,
          "127.0.0.1", 1200, true, false) &&
          !cudaFixedExperimentConfigurationValid(Selection::Wm8Runtime, true, false, false,
          "127.0.0.1", 1200, false, true), "WM8 public/override gate");
    check(!cudaFixedExperimentSelectable(Selection::Wm8Runtime, false) &&
          cudaFixedExperimentSelectable(Selection::Wm8Runtime, true) &&
          !cudaFixedExperimentVisits(Selection::Disabled, true, false) &&
          !cudaFixedExperimentVisits(Selection::Wm8Runtime, true, true) &&
          !cudaFixedExperimentVisits(static_cast<Selection>(255), false, false), "WM8 has no default/formal/fallback selection");
    PreparedJobRequest request;
    check(buildCudaPreparedJobRequest(plan, 4096, 65536, 120, PreparedJobMode::Normal, &request) &&
          request.preparationPlanId == plan.preparationPlanId, "WM8 exact canonical request");
    const auto previous = request;
    for (int arch : {80, 86, 89, 90, 100, 119, 121})
        check(!buildCudaPreparedJobRequest(plan, 4096, 65536, arch, PreparedJobMode::Normal, &request) &&
              sameRequest(request, previous), "WM8 rejects other SM transactionally");
    for (auto dims : {std::array<uint32_t, 2>{256,256}, {256,2048}, {4096,16384}, {4095,65536}})
        check(!buildCudaPreparedJobRequest(plan, dims[0], dims[1], 120, PreparedJobMode::Normal, &request) &&
              sameRequest(request, previous), "WM8 public request shape is exact production only");
    check(!buildCudaPreparedJobRequest(plan, 4096, 65536, 120, PreparedJobMode::SyntheticHardTarget, &request) &&
          sameRequest(request, previous), "WM8 rejects synthetic mode transactionally");
    auto altered = plan;
    altered.token = kCudaFixedExperimentPlan.token;
    check(!buildCudaPreparedJobRequest(altered, 4096, 65536, 120, PreparedJobMode::Normal, &request) &&
          sameRequest(request, previous), "WM8 cannot substitute fixed plan identity");
}

void wm8Cap112ExperimentTest() {
    using Selection = CudaFixedExperimentSelection;
    const auto &plan = kCudaWm8Cap112ExperimentPlan;
    check(kCudaPreparationPlanCount == 38 && cudaPreparationPlanCatalogValid(), "cap112 leaves default catalog intact");
    check(plan.token == CudaPreparationPlanToken::ExperimentalSm120Wm8InlineCap112 &&
          plan.preparationPlanId == UINT64_C(0x4355444100010029) &&
          std::string_view(plan.kernelId) == "cuda-ptx-mma-s8-sm120-wm8-runtime-inline-cap112-experimental-v1" &&
          std::string_view(plan.tileId) == "wm8-wn2-tm2-tn4-bk64-st3-gm8", "cap112 exact independent inline identity");
    check(plan.wm == 8 && plan.wn == 2 && plan.tm == 2 && plan.tn == 4 &&
          plan.threads == 512 && plan.blockM == 256 && plan.blockN == 128 &&
          plan.blockK == 64 && plan.stages == 3 && plan.groupM == 8 && plan.nMajorB,
          "cap112 exact runtime WM8 geometry");
    check(findCudaPreparationPlan(plan.token) == &plan &&
          findCudaPreparationPlan(plan.kernelId, plan.tileId) == &plan, "cap112 canonical lookup");
    check(parseCudaFixedExperiment(kCudaWm8Cap112ExperimentOptIn) == Selection::Wm8InlineCap112 &&
          parseCudaFixedExperiment(kCudaWm8ExperimentOptIn) == Selection::Wm8Runtime &&
          parseCudaFixedExperiment(nullptr) == Selection::Disabled, "cap112 exact selector not default/WM8");
    for (const char *value : {"", "cap112", "sm120-wm8-wn2-tm2-tn4-runtime-inline-cap112-v1 ",
                             "sm120-wm8-wn2-tm2-tn4-runtime-inline-cap120-v1",
                             "sm120-wm8-wn2-tm2-tn4-runtime-cap112-v1"})
        check(parseCudaFixedExperiment(value) == Selection::Invalid, "unregistered cap selector refused");
    for (bool fixed : {false, true}) for (bool wm8 : {false, true}) for (bool cap : {false, true}) {
        check(cudaIsolatedExperimentCompiled(Selection::Wm8InlineCap112, fixed, wm8, cap) == cap &&
              cudaIsolatedExperimentCompiled(Selection::Enabled, fixed, wm8, cap) == fixed &&
              cudaIsolatedExperimentCompiled(Selection::Wm8Runtime, fixed, wm8, cap) == wm8 &&
              !cudaIsolatedExperimentCompiled(Selection::Invalid, fixed, wm8, cap) &&
              !cudaIsolatedExperimentCompiled(Selection::Disabled, fixed, wm8, cap), "cap112 independent build capability");
    }
    for (bool compiled : {false, true}) for (bool formal : {false, true}) for (bool bench : {false, true})
        check(cudaFixedExperimentConfigurationValid(Selection::Wm8InlineCap112, compiled, formal, bench,
              "127.0.0.1", 1200, false, false) == (compiled && !formal && !bench), "cap112 admission matrix");
    for (const char *host : {"", "localhost", "::1", "127.0.0.2", "public.example", "127.0.0.1.evil"})
        check(!cudaFixedExperimentConfigurationValid(Selection::Wm8InlineCap112, true, false, false,
              host, 1200, false, false), "cap112 literal loopback only");
    for (int port : {-1, 0, 65536})
        check(!cudaFixedExperimentConfigurationValid(Selection::Wm8InlineCap112, true, false, false,
              "127.0.0.1", port, false, false), "cap112 bounded port");
    for (auto flags : {std::array<bool, 2>{true,false}, {false,true}, {true,true}})
        check(!cudaFixedExperimentConfigurationValid(Selection::Wm8InlineCap112, true, false, false,
              "127.0.0.1", 1200, flags[0], flags[1]), "cap112 rejects conflicting overrides");
    check(!cudaFixedExperimentSelectable(Selection::Wm8InlineCap112, false) &&
          cudaFixedExperimentSelectable(Selection::Wm8InlineCap112, true) &&
          !cudaFixedExperimentVisits(Selection::Disabled, true, false) &&
          !cudaFixedExperimentVisits(Selection::Wm8InlineCap112, true, true), "cap112 no default/formal/fallback selection");
    PreparedJobRequest request;
    check(buildCudaPreparedJobRequest(plan, 4096, 65536, 120, PreparedJobMode::Normal, &request) &&
          request.preparationPlanId == plan.preparationPlanId && request.config.commonDim == 2048 &&
          request.config.rank == 128, "cap112 request binds actual new plan");
    const auto previous = request;
    for (int arch : {0, 80, 86, 89, 90, 100, 119, 121, 130})
        check(!buildCudaPreparedJobRequest(plan, 4096, 65536, arch, PreparedJobMode::Normal, &request) &&
              sameRequest(request, previous), "cap112 rejects other SM transactionally");
    for (auto dims : {std::array<uint32_t, 2>{0,65536}, {256,256}, {4096,16384}, {4095,65536}, {4096,65535}})
        check(!buildCudaPreparedJobRequest(plan, dims[0], dims[1], 120, PreparedJobMode::Normal, &request) &&
              sameRequest(request, previous), "cap112 exact canonical production shape");
    check(!buildCudaPreparedJobRequest(plan, 4096, 65536, 120, PreparedJobMode::SyntheticHardTarget, &request) &&
          sameRequest(request, previous), "cap112 rejects synthetic request");
    auto altered = plan;
    altered.preparationPlanId = kCudaWm8ExperimentPlan.preparationPlanId;
    check(!buildCudaPreparedJobRequest(altered, 4096, 65536, 120, PreparedJobMode::Normal, &request) &&
          sameRequest(request, previous), "cap112 cannot masquerade as original WM8 plan");
    altered = plan;
    altered.kernelId = kCudaWm8ExperimentPlan.kernelId;
    check(!buildCudaPreparedJobRequest(altered, 4096, 65536, 120, PreparedJobMode::Normal, &request) &&
          sameRequest(request, previous), "cap112 cannot substitute original kernel label");
}

void wm8FixedExperimentTest() {
    using S = CudaFixedExperimentSelection;
    constexpr auto selection = S::Wm8FixedK2048R128;
    const auto &plan = kCudaWm8FixedExperimentPlan;
    check(kCudaPreparationPlanCount == 38 && cudaPreparationPlanCatalogValid(), "fixed WM8 default catalog untouched");
    check(plan.token == CudaPreparationPlanToken::ExperimentalSm120Wm8FixedK2048R128 &&
          plan.preparationPlanId == UINT64_C(0x435544410001002a) &&
          std::string_view(plan.kernelId) == "cuda-ptx-mma-s8-sm120-wm8-fixed-k2048-r128-experimental-v1" &&
          std::string_view(plan.tileId) == "wm8-wn2-tm2-tn4-bk64-st3-gm8", "fixed WM8 permanent identity");
    check(plan.wm == 8 && plan.wn == 2 && plan.tm == 2 && plan.tn == 4 &&
          plan.blockM == 256 && plan.blockN == 128 && plan.threads == 512 &&
          plan.blockK == 64 && plan.stages == 3 && plan.groupM == 8 && plan.nMajorB &&
          plan.family == kPtx && 3 * (plan.blockM + plan.blockN) * (plan.blockK + 16) == 92160,
          "fixed WM8 exact launch geometry and shared memory");
    check(findCudaPreparationPlan(plan.token) == &plan &&
          findCudaPreparationPlan(plan.kernelId, plan.tileId) == &plan, "fixed WM8 canonical lookup");
    check(parseCudaFixedExperiment(kCudaWm8FixedExperimentOptIn) == selection, "fixed WM8 closed selector");
    for (const char *value : {"", "fixed-wm8", "sm120-wm8-wn2-tm2-tn4-fixed-k2048-r128-v1 ",
                              "sm120-wm8-wn2-tm2-tn4-fixed-k2048-r128-v2"})
        check(parseCudaFixedExperiment(value) == S::Invalid, "fixed WM8 malformed selectors refused");
    for (bool oldFixed : {false,true}) for (bool runtime : {false,true})
    for (bool capped : {false,true}) for (bool fixed : {false,true}) {
        check(cudaIsolatedExperimentCompiled(selection,oldFixed,runtime,capped,fixed) == fixed &&
              cudaIsolatedExperimentCompiled(S::Enabled,oldFixed,runtime,capped,fixed) == oldFixed &&
              cudaIsolatedExperimentCompiled(S::Wm8Runtime,oldFixed,runtime,capped,fixed) == runtime &&
              cudaIsolatedExperimentCompiled(S::Wm8InlineCap112,oldFixed,runtime,capped,fixed) == capped,
              "all four compile capabilities independent");
    }
    check(!cudaIsolatedExperimentCompiled(selection,true,true,true), "old caller cannot implicitly enable fixed WM8");
    for (unsigned flags = 0; flags < 256; ++flags) {
        const bool compiled = flags & 1, formal = flags & 2, synthetic = flags & 4;
        check(cudaFixedExperimentConfigurationValid(selection,compiled,formal,synthetic,"127.0.0.1",1200,
                flags & 8,flags & 16,flags & 32,flags & 64,flags & 128) == (flags == 1),
              "fixed WM8 rejects every incompatible admission combination");
    }
    for (const char *host : {"", "localhost", "::1", "127.0.0.2", "public.example"})
        check(!cudaFixedExperimentConfigurationValid(selection,true,false,false,host,1200,false,false),
              "fixed WM8 literal loopback only");
    for (int port : {-1,0,65536})
        check(!cudaFixedExperimentConfigurationValid(selection,true,false,false,"127.0.0.1",port,false,false),
              "fixed WM8 bounded port");
    for (bool captured : {false,true}) for (bool current : {false,true})
        check(cudaWm8FixedEnvironmentValid(selection,kCudaWm8FixedExperimentOptIn,captured,current) ==
              (!captured && !current), "captured removal/current insertion cannot bypass isolation");
    for (const char *value : {static_cast<const char *>(nullptr), "", kCudaWm8ExperimentOptIn,
                              kCudaWm8Cap112ExperimentOptIn,kCudaFixedExperimentOptIn})
        check(!cudaWm8FixedEnvironmentValid(selection,value,false,false), "changed/removed selector refused");
    check(!cudaFixedExperimentVisits(S::Disabled,true,false) &&
          !cudaFixedExperimentVisits(selection,true,true) &&
          cudaFixedExperimentVisits(selection,false,false) &&
          !cudaFixedExperimentSelectable(selection,false) && cudaFixedExperimentSelectable(selection,true),
          "default exclusion and original witness population without fallback");
    PreparedJobRequest request;
    check(buildCudaPreparedJobRequest(plan,4096,65536,120,PreparedJobMode::Normal,&request) &&
          request.preparationPlanId == plan.preparationPlanId && request.config.commonDim == 2048 &&
          request.config.rank == 128, "fixed WM8 canonical request");
    const auto before = request;
    for (int arch : {0,80,86,89,90,100,119,121,130})
        check(!buildCudaPreparedJobRequest(plan,4096,65536,arch,PreparedJobMode::Normal,&request) &&
              sameRequest(request,before), "fixed WM8 exact SM transactionally enforced");
    for (auto dims : {std::array<uint32_t,2>{256,256}, {2304,512}, {4095,65536}, {4096,65535}})
        check(!buildCudaPreparedJobRequest(plan,dims[0],dims[1],120,PreparedJobMode::Normal,&request) &&
              sameRequest(request,before), "fixed WM8 production-only request dimensions");
    check(!buildCudaPreparedJobRequest(plan,4096,65536,120,PreparedJobMode::SyntheticHardTarget,&request) &&
          sameRequest(request,before), "fixed WM8 rejects synthetic request");
    for (const auto *prior : {&kCudaFixedExperimentPlan,&kCudaWm8ExperimentPlan,&kCudaWm8Cap112ExperimentPlan}) {
        auto altered = plan; altered.preparationPlanId = prior->preparationPlanId;
        check(!buildCudaPreparedJobRequest(altered,4096,65536,120,PreparedJobMode::Normal,&request) &&
              sameRequest(request,before), "fixed WM8 cannot reuse prior cache identity");
        altered = plan; altered.kernelId = prior->kernelId;
        check(!buildCudaPreparedJobRequest(altered,4096,65536,120,PreparedJobMode::Normal,&request) &&
              sameRequest(request,before), "fixed WM8 cannot substitute prior kernel identity");
    }
    check(cudaFixedExperimentLaunchValid(120,256,256,2048,128) &&
          !cudaFixedExperimentLaunchValid(120,4096,65536,2047,128) &&
          !cudaFixedExperimentLaunchValid(120,4096,65536,2048,64), "self-check allowed but wrong K/rank refused");
}

void bk32FixedExperimentTest() {
    using S = CudaFixedExperimentSelection;
    constexpr auto selection = S::Bk32FixedK2048R128;
    const auto &plan = kCudaBk32FixedExperimentPlan;
    check(kCudaPreparationPlanCount == 38 && cudaPreparationPlanCatalogValid(), "BK32 preserves default catalog");
    check(plan.token == CudaPreparationPlanToken::ExperimentalSm120Bk32FixedK2048R128 &&
          plan.preparationPlanId == UINT64_C(0x435544410001002c) &&
          std::string_view(plan.kernelId) == "cuda-ptx-mma-s8-sm120-bk32-fixed-k2048-r128-experimental-v1" &&
          std::string_view(plan.tileId) == "wm2-wn4-tm4-tn2-bk32-st3-gm8", "BK32 permanent identity");
    check(plan.wm == 2 && plan.wn == 4 && plan.tm == 4 && plan.tn == 2 &&
          plan.blockM == 128 && plan.blockN == 128 && plan.threads == 256 &&
          plan.blockK == 32 && plan.stages == 3 && plan.groupM == 8 && plan.nMajorB &&
          plan.family == kPtx && plan.architecture.match == TileArchMatch::Exact &&
          plan.architecture.capability == 120 &&
          3 * (plan.blockM + plan.blockN) * (plan.blockK + 16) == 36864, "BK32 exact geometry/resources");
    check(findCudaPreparationPlan(plan.token) == &plan &&
          findCudaPreparationPlan(plan.kernelId,plan.tileId) == &plan, "BK32 canonical lookup");
    check(parseCudaFixedExperiment(kCudaBk32FixedExperimentOptIn) == selection, "BK32 closed selector");
    for (const char *value : {"", "retired-fixed", "sm120-wm2-wn4-tm4-tn2-bk32-fixed-k2048-r128-v1 ",
                             "sm86-wm2-wn4-tm4-tn2-fixed-k2048-r128-v1"})
        check(parseCudaFixedExperiment(value) == S::Invalid, "BK32 malformed selector refused");
    for (bool a : {false,true}) for (bool b : {false,true}) for (bool c : {false,true})
    for (bool d : {false,true}) for (bool e : {false,true}) {
        check(cudaIsolatedExperimentCompiled(selection,a,b,c,d,e) == e &&
              cudaIsolatedExperimentCompiled(S::Enabled,a,b,c,d,e) == a &&
              cudaIsolatedExperimentCompiled(S::Wm8Runtime,a,b,c,d,e) == b &&
              cudaIsolatedExperimentCompiled(S::Wm8InlineCap112,a,b,c,d,e) == c &&
              cudaIsolatedExperimentCompiled(S::Wm8FixedK2048R128,a,b,c,d,e) == d &&
              !cudaIsolatedExperimentCompiled(S::Invalid,a,b,c,d,e) &&
              !cudaIsolatedExperimentCompiled(S::Disabled,a,b,c,d,e), "five independent capabilities");
    }
    check(!cudaIsolatedExperimentCompiled(selection,true,true,true,true), "old callers cannot enable BK32");
    for (unsigned flags=0; flags<256; ++flags)
        check(cudaFixedExperimentConfigurationValid(selection,flags&1,flags&2,flags&4,"127.0.0.1",1200,
                  flags&8,flags&16,flags&32,flags&64,flags&128) == (flags==1), "BK32 isolated admission matrix");
    for (const char *host : {"", "localhost", "::1", "127.0.0.2", "public.example"})
        check(!cudaFixedExperimentConfigurationValid(selection,true,false,false,host,1200,false,false), "BK32 literal loopback");
    for (int port : {-1,0,65536})
        check(!cudaFixedExperimentConfigurationValid(selection,true,false,false,"127.0.0.1",port,false,false), "BK32 bounded port");
    for (bool captured : {false,true}) for (bool current : {false,true})
        check(cudaWm8FixedEnvironmentValid(selection,kCudaBk32FixedExperimentOptIn,captured,current) ==
              (!captured && !current), "BK32 sticky/current conflicts refused");
    for (const char *value : {static_cast<const char *>(nullptr), "", kCudaFixedExperimentOptIn,
                             kCudaWm8ExperimentOptIn,kCudaWm8FixedExperimentOptIn,kCudaWm8Cap112ExperimentOptIn})
        check(!cudaWm8FixedEnvironmentValid(selection,value,false,false), "BK32 selector removal/change refused");
    for (int arch : {0,70,75,80,86,88,89,90,100,119,120,121,130}) {
        check(cudaSelectedExperimentLaunchValid(selection,arch,4096,65536,2048,128) == (arch==120) &&
              cudaSelectedExperimentLaunchValid(selection,arch,256,256,2048,128) == (arch==120), "BK32 exact launch architecture");
        for (auto old : {S::Enabled,S::Wm8Runtime,S::Wm8InlineCap112,S::Wm8FixedK2048R128})
            check(cudaSelectedExperimentLaunchValid(old,arch,4096,65536,2048,128) ==
                  cudaFixedExperimentLaunchValid(arch,4096,65536,2048,128), "old SM120 launches unchanged");
    }
    for (auto bad : {S::Disabled,S::Invalid})
        check(!cudaSelectedExperimentLaunchValid(bad,120,4096,65536,2048,128), "disabled/invalid cannot launch");
    for (auto dims : {std::array<int,4>{0,65536,2048,128}, {2048,16384,2048,128},
                     {4095,65536,2048,128}, {4096,65535,2048,128}, {4096,65536,0,128},
                     {4096,65536,2047,128}, {4096,65536,2048,0}, {4096,65536,2048,64},
                     {-1,65536,2048,128}, {4096,65536,-1,128}, {4096,65536,2048,-1}})
        check(!cudaSelectedExperimentLaunchValid(selection,120,dims[0],dims[1],dims[2],dims[3]), "BK32 invalid shape/K/rank refused");
    check(!cudaFixedExperimentVisits(S::Disabled,true,false) && !cudaFixedExperimentVisits(selection,true,true) &&
          cudaFixedExperimentVisits(selection,false,false) && !cudaFixedExperimentSelectable(selection,false) &&
          cudaFixedExperimentSelectable(selection,true), "BK32 default exclusion/quorum/no fallback");
    PreparedJobRequest request;
    check(buildCudaPreparedJobRequest(plan,4096,65536,120,PreparedJobMode::Normal,&request) &&
          request.preparationPlanId == plan.preparationPlanId && request.config.commonDim == 2048 &&
          request.config.rank == 128, "BK32 canonical request");
    const auto before=request;
    for (int arch : {0,86,88,89,90,119,121})
        check(!buildCudaPreparedJobRequest(plan,4096,65536,arch,PreparedJobMode::Normal,&request) &&
              sameRequest(request,before), "BK32 wrong architecture transactionally refused");
    for (auto dims : {std::array<uint32_t,2>{256,256}, {2048,16384}, {2304,512}, {4095,65536}})
        check(!buildCudaPreparedJobRequest(plan,dims[0],dims[1],120,PreparedJobMode::Normal,&request) &&
              sameRequest(request,before), "BK32 production request shape immutable");
    check(!buildCudaPreparedJobRequest(plan,4096,65536,120,PreparedJobMode::SyntheticHardTarget,&request) &&
          sameRequest(request,before), "BK32 synthetic request refused");
    for (const auto *old : {&kCudaFixedExperimentPlan,&kCudaWm8ExperimentPlan,&kCudaWm8Cap112ExperimentPlan,
                           &kCudaWm8FixedExperimentPlan,&kCudaPreparationPlans[19]}) {
        auto altered=plan; altered.preparationPlanId=old->preparationPlanId;
        check(!buildCudaPreparedJobRequest(altered,4096,65536,120,PreparedJobMode::Normal,&request) &&
              sameRequest(request,before), "BK32 cannot alias earlier cache plan");
        altered=plan; altered.kernelId=old->kernelId;
        check(!buildCudaPreparedJobRequest(altered,4096,65536,120,PreparedJobMode::Normal,&request) &&
              sameRequest(request,before), "BK32 cannot alias earlier kernel identity");
    }
}

void wm4ReuseExperimentTest() {
    using S = CudaFixedExperimentSelection;
    constexpr auto selection = S::Wm4ReuseFixedK2048R128;
    const auto &plan = kCudaWm4ReuseExperimentPlan;
    check(kCudaPreparationPlanCount == 38 && cudaPreparationPlanCatalogValid(), "WM4 reuse preserves default catalog");
    check(plan.token == CudaPreparationPlanToken::ExperimentalSm120Wm4ReuseFixedK2048R128 &&
          plan.preparationPlanId == UINT64_C(0x435544410001002d) &&
          std::string_view(plan.kernelId) == "cuda-ptx-mma-s8-sm120-wm4-reuse-fixed-k2048-r128-experimental-v1" &&
          std::string_view(plan.tileId) == "wm4-wn2-tm4-tn4-bk64-st3-gm8", "WM4 reuse permanent identity");
    check(plan.wm == 4 && plan.wn == 2 && plan.tm == 4 && plan.tn == 4 &&
          plan.blockM == 256 && plan.blockN == 128 && plan.threads == 256 &&
          plan.blockK == 64 && plan.stages == 3 && plan.groupM == 8 && plan.nMajorB &&
          plan.family == kPtx && plan.architecture.match == TileArchMatch::Exact &&
          plan.architecture.capability == 120 &&
          3 * (plan.blockM + plan.blockN) * (plan.blockK + 16) == 92160, "WM4 reuse exact geometry/resources");
    check(findCudaPreparationPlan(plan.token) == &plan &&
          findCudaPreparationPlan(plan.kernelId,plan.tileId) == &plan, "WM4 reuse canonical lookup");
    check(parseCudaFixedExperiment(kCudaWm4ReuseExperimentOptIn) == selection, "WM4 reuse closed selector");
    for (const char *value : {"", "retired-fixed", "sm120-wm4-reuse-wn2-tm4-tn4-fixed-k2048-r128-v1 ",
                             "sm86-wm2-wn4-tm4-tn2-fixed-k2048-r128-v1"})
        check(parseCudaFixedExperiment(value) == S::Invalid, "WM4 reuse malformed selector refused");
    for (bool a : {false,true}) for (bool b : {false,true}) for (bool c : {false,true})
    for (bool d : {false,true}) for (bool e : {false,true}) for (bool f : {false,true}) {
        check(cudaIsolatedExperimentCompiled(selection,a,b,c,d,e,f) == f &&
              cudaIsolatedExperimentCompiled(S::Enabled,a,b,c,d,e,f) == a &&
              cudaIsolatedExperimentCompiled(S::Wm8Runtime,a,b,c,d,e,f) == b &&
              cudaIsolatedExperimentCompiled(S::Wm8InlineCap112,a,b,c,d,e,f) == c &&
              cudaIsolatedExperimentCompiled(S::Wm8FixedK2048R128,a,b,c,d,e,f) == d &&
              cudaIsolatedExperimentCompiled(S::Bk32FixedK2048R128,a,b,c,d,e,f) == e &&
              !cudaIsolatedExperimentCompiled(S::Invalid,a,b,c,d,e,f) &&
              !cudaIsolatedExperimentCompiled(S::Disabled,a,b,c,d,e,f), "six independent capabilities");
    }
    check(!cudaIsolatedExperimentCompiled(selection,true,true,true,true,true), "old callers cannot enable WM4 reuse");
    for (unsigned flags=0; flags<256; ++flags)
        check(cudaFixedExperimentConfigurationValid(selection,flags&1,flags&2,flags&4,"127.0.0.1",1200,
                  flags&8,flags&16,flags&32,flags&64,flags&128) == (flags==1), "WM4 reuse isolated admission matrix");
    for (const char *host : {"", "localhost", "::1", "127.0.0.2", "public.example"})
        check(!cudaFixedExperimentConfigurationValid(selection,true,false,false,host,1200,false,false), "WM4 reuse literal loopback");
    for (int port : {-1,0,65536})
        check(!cudaFixedExperimentConfigurationValid(selection,true,false,false,"127.0.0.1",port,false,false), "WM4 reuse bounded port");
    for (bool captured : {false,true}) for (bool current : {false,true})
        check(cudaWm8FixedEnvironmentValid(selection,kCudaWm4ReuseExperimentOptIn,captured,current) ==
              (!captured && !current), "WM4 reuse sticky/current conflicts refused");
    for (const char *value : {static_cast<const char *>(nullptr), "", kCudaFixedExperimentOptIn,
                             kCudaWm8ExperimentOptIn,kCudaWm8FixedExperimentOptIn,kCudaWm8Cap112ExperimentOptIn,kCudaBk32FixedExperimentOptIn})
        check(!cudaWm8FixedEnvironmentValid(selection,value,false,false), "WM4 reuse selector removal/change refused");
    for (int arch : {0,70,75,80,86,88,89,90,100,119,120,121,130}) {
        check(cudaSelectedExperimentLaunchValid(selection,arch,4096,65536,2048,128) == (arch==120) &&
              cudaSelectedExperimentLaunchValid(selection,arch,256,256,2048,128) == (arch==120), "WM4 reuse exact launch architecture");
        for (auto old : {S::Enabled,S::Wm8Runtime,S::Wm8InlineCap112,S::Wm8FixedK2048R128})
            check(cudaSelectedExperimentLaunchValid(old,arch,4096,65536,2048,128) ==
                  cudaFixedExperimentLaunchValid(arch,4096,65536,2048,128), "old SM120 launches unchanged");
    }
    for (auto bad : {S::Disabled,S::Invalid})
        check(!cudaSelectedExperimentLaunchValid(bad,120,4096,65536,2048,128), "disabled/invalid cannot launch");
    for (auto dims : {std::array<int,4>{0,65536,2048,128}, {2048,16384,2048,128},
                     {4095,65536,2048,128}, {4096,65535,2048,128}, {4096,65536,0,128},
                     {4096,65536,2047,128}, {4096,65536,2048,0}, {4096,65536,2048,64},
                     {-1,65536,2048,128}, {4096,65536,-1,128}, {4096,65536,2048,-1}})
        check(!cudaSelectedExperimentLaunchValid(selection,120,dims[0],dims[1],dims[2],dims[3]), "WM4 reuse invalid shape/K/rank refused");
    check(!cudaFixedExperimentVisits(S::Disabled,true,false) && !cudaFixedExperimentVisits(selection,true,true) &&
          cudaFixedExperimentVisits(selection,false,false) && !cudaFixedExperimentSelectable(selection,false) &&
          cudaFixedExperimentSelectable(selection,true), "WM4 reuse default exclusion/quorum/no fallback");
    PreparedJobRequest request;
    check(buildCudaPreparedJobRequest(plan,4096,65536,120,PreparedJobMode::Normal,&request) &&
          request.preparationPlanId == plan.preparationPlanId && request.config.commonDim == 2048 &&
          request.config.rank == 128, "WM4 reuse canonical request");
    const auto before=request;
    for (int arch : {0,86,88,89,90,119,121})
        check(!buildCudaPreparedJobRequest(plan,4096,65536,arch,PreparedJobMode::Normal,&request) &&
              sameRequest(request,before), "WM4 reuse wrong architecture transactionally refused");
    for (auto dims : {std::array<uint32_t,2>{256,256}, {2048,16384}, {2304,512}, {4095,65536}})
        check(!buildCudaPreparedJobRequest(plan,dims[0],dims[1],120,PreparedJobMode::Normal,&request) &&
              sameRequest(request,before), "WM4 reuse production request shape immutable");
    check(!buildCudaPreparedJobRequest(plan,4096,65536,120,PreparedJobMode::SyntheticHardTarget,&request) &&
          sameRequest(request,before), "WM4 reuse synthetic request refused");
    for (const auto *old : {&kCudaFixedExperimentPlan,&kCudaWm8ExperimentPlan,&kCudaWm8Cap112ExperimentPlan,
                           &kCudaWm8FixedExperimentPlan,&kCudaBk32FixedExperimentPlan,&kCudaPreparationPlans[19]}) {
        auto altered=plan; altered.preparationPlanId=old->preparationPlanId;
        check(!buildCudaPreparedJobRequest(altered,4096,65536,120,PreparedJobMode::Normal,&request) &&
              sameRequest(request,before), "WM4 reuse cannot alias earlier cache plan");
        altered=plan; altered.kernelId=old->kernelId;
        check(!buildCudaPreparedJobRequest(altered,4096,65536,120,PreparedJobMode::Normal,&request) &&
              sameRequest(request,before), "WM4 reuse cannot alias earlier kernel identity");
    }
}

}  // namespace

int main() {
    catalogGoldenTest();
    requestValidationTest();
    metadataMismatchTest();
    canonicalBuilderBridgeTest();
    fixedExperimentTest();
    wm8ExperimentTest();
    wm8Cap112ExperimentTest();
    wm8FixedExperimentTest();
    bk32FixedExperimentTest();
    wm4ReuseExperimentTest();
    if (failures) {
        std::fprintf(stderr, "Pearl CUDA preparation plans: %d failure(s)\n",
                     failures);
        return 1;
    }
    std::printf("Pearl CUDA preparation plans: all checks passed\n");
    return 0;
}
