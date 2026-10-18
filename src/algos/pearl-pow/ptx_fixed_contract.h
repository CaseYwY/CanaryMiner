#pragma once
#if defined(CANARY_PEARL_ENABLE_WM4_REUSE_EXPERIMENT)
#if CANARY_PEARL_ENABLE_WM4_REUSE_EXPERIMENT != 1
#error "CANARY_PEARL_ENABLE_WM4_REUSE_EXPERIMENT must be absent or exactly 1"
#endif
#if defined(CANARY_PEARL_TEST_PTX_INLINE_BODY) || defined(CANARY_PEARL_TEST_WM8_REGCAP) || defined(CANARY_PEARL_ENABLE_WM8_CAP112_EXPERIMENT) || defined(CANARY_PEARL_DIRECT_FOLD_MODE)
#error "WM4 reuse backend requires original global body, launch qualifier and general fold"
#endif
#if defined(CANARY_PEARL_TEST_RESOURCE_ONLY) || defined(CANARY_PEARL_TEST_WM4_REUSE_RESOURCE) || defined(CANARY_PEARL_TEST_WM8_FIXED_RESOURCE) || defined(CANARY_PEARL_TEST_BK32_FIXED_RESOURCE)
#error "WM4 reuse backend cannot combine standalone resource-probe admission"
#endif
static_assert(CANARY_PEARL_ENABLE_WM4_REUSE_EXPERIMENT == 1,
              "WM4 reuse backend requires a valid explicit enable token");
#endif
#if defined(CANARY_PEARL_ENABLE_BK32_FIXED_EXPERIMENT) && CANARY_PEARL_ENABLE_BK32_FIXED_EXPERIMENT != 1
#error "CANARY_PEARL_ENABLE_BK32_FIXED_EXPERIMENT must be absent or exactly 1"
#endif
#if defined(CANARY_PEARL_ENABLE_BK32_FIXED_EXPERIMENT)
#if defined(CANARY_PEARL_TEST_PTX_INLINE_BODY) || defined(CANARY_PEARL_TEST_WM8_REGCAP) || defined(CANARY_PEARL_ENABLE_WM8_CAP112_EXPERIMENT) || defined(CANARY_PEARL_DIRECT_FOLD_MODE)
#error "BK32 fixed backend requires original global body, launch qualifier and general fold"
#endif
#endif
namespace om::pearl {
// Compile-time admission only. Runtime matrix-stride checks remain in the
// original kernel body; this does not turn any runtime launch into fixed work.
#if defined(__CUDACC__)
__host__ __device__
#endif
constexpr bool ptxFixedTupleAllowed(int wm, int wn, int tm, int tn,
                                    int bk, int stages, int groupM) noexcept {
#if defined(CANARY_PEARL_ENABLE_WM4_REUSE_EXPERIMENT)
    // Default-off exact SM120 host-dispatched candidate. The original body
    // still refuses runtime K/rank mismatch before touching any input/output.
    if (wm == 4 && wn == 2 && tm == 4 && tn == 4 &&
        bk == 64 && stages == 3 && groupM == 8) return true;
#endif
#if defined(CANARY_PEARL_ENABLE_BK32_FIXED_EXPERIMENT)
    // Separately selected SM120 experiment only. Default compile admission is
    // unchanged; architecture/shape/runtime stride checks remain host-enforced.
    if (wm == 2 && wn == 4 && tm == 4 && tn == 2 &&
        bk == 32 && stages == 3 && groupM == 8) return true;
#endif
    return ((((wm == 4 && wn == 4) || (wm == 8 && wn == 2)) && tm == 2 && tn == 4) ||
            (wm == 2 && wn == 4 && tm == 4 && tn == 2)) &&
        bk == 64 && stages == 3 && groupM == 8;
}
#if defined(__CUDACC__)
__host__ __device__
#endif
constexpr bool ptxDirectFoldTupleAllowed(int wm, int wn, int tm, int tn,
                                         int bk, int stages, int groupM, bool fixed) noexcept {
    return fixed && wm == 4 && wn == 4 && tm == 2 && tn == 4 &&
        bk == 64 && stages == 3 && groupM == 8;
}
} // namespace om::pearl
