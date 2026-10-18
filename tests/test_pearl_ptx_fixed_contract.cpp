#include "../src/algos/pearl-pow/ptx_fixed_contract.h"
#include <cassert>
#include <initializer_list>

using namespace om::pearl;
static_assert(ptxFixedTupleAllowed(4,4,2,4,64,3,8), "old fixed tile remains legal");
static_assert(ptxFixedTupleAllowed(8,2,2,4,64,3,8), "new exact fixed WM8");
static_assert(ptxFixedTupleAllowed(2,4,4,2,64,3,8), "exact fixed geometry");
static_assert(!ptxDirectFoldTupleAllowed(2,4,4,2,64,3,8,true), "fixed tuple never inherits direct fold");
static_assert(!ptxDirectFoldTupleAllowed(8,2,2,4,64,3,8,true), "WM8 never inherits direct fold");
static_assert(ptxDirectFoldTupleAllowed(4,4,2,4,64,3,8,true), "original direct fold unchanged");
#if defined(CANARY_PEARL_ENABLE_BK32_FIXED_EXPERIMENT)
static_assert(ptxFixedTupleAllowed(2,4,4,2,32,3,8), "explicit exact BK32 fixed geometry");
#else
static_assert(!ptxFixedTupleAllowed(2,4,4,2,32,3,8), "default BK32 fixed admission remains absent");
#endif
static_assert(!ptxDirectFoldTupleAllowed(2,4,4,2,32,3,8,true), "BK32 never inherits direct fold");
#if defined(CANARY_PEARL_ENABLE_WM4_REUSE_EXPERIMENT)
static_assert(ptxFixedTupleAllowed(4,2,4,4,64,3,8), "explicit exact WM4 reuse fixed geometry");
#else
static_assert(!ptxFixedTupleAllowed(4,2,4,4,64,3,8), "WM4 reuse absent from default admission");
#endif
static_assert(!ptxDirectFoldTupleAllowed(4,2,4,4,64,3,8,true), "WM4 reuse never inherits direct fold");
int main() {
    unsigned fixedCount = 0, directCount = 0;
    for (int wm : {0,2,4,8}) for (int wn : {0,2,4,8})
    for (int tm : {0,1,2,4}) for (int tn : {0,1,2,4})
    for (int bk : {0,32,64,128}) for (int st : {0,2,3,4}) for (int gm : {0,4,8,16}) {
        const bool fixed = ptxFixedTupleAllowed(wm,wn,tm,tn,bk,st,gm);
        const bool direct = ptxDirectFoldTupleAllowed(wm,wn,tm,tn,bk,st,gm,true);
        fixedCount += fixed; directCount += direct;
        assert(!direct || fixed);
        assert(!ptxDirectFoldTupleAllowed(wm,wn,tm,tn,bk,st,gm,false));
    }
    unsigned expected = 3;
#if defined(CANARY_PEARL_ENABLE_BK32_FIXED_EXPERIMENT)
    ++expected;
#endif
#if defined(CANARY_PEARL_ENABLE_WM4_REUSE_EXPERIMENT)
    ++expected;
#endif
    assert(fixedCount == expected && directCount == 1);
    assert(!ptxFixedTupleAllowed(-4,4,2,4,64,3,8));
    assert(!ptxFixedTupleAllowed(8,2,2,4,64,3,-8));
}
