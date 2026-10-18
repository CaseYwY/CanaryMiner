#pragma once
#include "pearl_private_execution_types.h"
#include "pearl_native_selection.h"
#include "pearl_sm120_native_selection.h"
#include <cstdlib>
#include <cstring>
#include <initializer_list>
namespace om {
inline PearlPrivateExecution pearlParsePrivateExecution(const char *profile,const char *mode) noexcept {
    if (!profile && !mode) return PearlPrivateExecution::Disabled;
    if (profile && mode && !std::strcmp(profile,kPearlPrivateGenericProfile))
        return !std::strcmp(mode,"serial")?PearlPrivateExecution::GenericSerial:
            !std::strcmp(mode,"a-prep-v1")?PearlPrivateExecution::GenericAPrep:PearlPrivateExecution::Invalid;
    if (!profile || std::strcmp(profile,kPearlPrivateProfile) || !mode) return PearlPrivateExecution::Invalid;
    return !std::strcmp(mode,"serial")?PearlPrivateExecution::Serial:
        !std::strcmp(mode,"a-prep64-v1")?PearlPrivateExecution::APrep64:PearlPrivateExecution::Invalid;
}
inline bool pearlPrivateCompiled(PearlPrivateExecution mode=PearlPrivateExecution::Serial) noexcept {
    if (pearlPrivateGeneric(mode)) {
#if defined(CANARY_PEARL_ENABLE_PRIVATE_GENERIC_LOOKAHEAD) && CANARY_PEARL_ENABLE_PRIVATE_GENERIC_LOOKAHEAD == 1 && defined(CANARY_PEARL_ONLY_BUILD) && CANARY_PEARL_ONLY_BUILD == 1 && defined(CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT) && defined(CANARY_PEARL_ENABLE_SM120_NATIVE_EVIDENCE) && CANARY_PEARL_ENABLE_SM120_NATIVE_EVIDENCE == 1 && defined(CANARY_PEARL_ENABLE_ATTEMPT_OVERLAP) && CANARY_PEARL_ENABLE_ATTEMPT_OVERLAP == 1
        return true;
#else
        return false;
#endif
    }

#if defined(CANARY_PEARL_ENABLE_PRIVATE_LOOKAHEAD) && CANARY_PEARL_ENABLE_PRIVATE_LOOKAHEAD == 1 && defined(CANARY_PEARL_ONLY_BUILD) && CANARY_PEARL_ONLY_BUILD == 1 && defined(CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT) && defined(CANARY_PEARL_ENABLE_WM8_EXPERIMENT) && CANARY_PEARL_ENABLE_WM8_EXPERIMENT == 1 && defined(CANARY_PEARL_ENABLE_ATTEMPT_OVERLAP) && CANARY_PEARL_ENABLE_ATTEMPT_OVERLAP == 1
    return true;
#else
    return false;
#endif
}
inline bool pearlPrivateEnvironment(PearlPrivateExecution captured) noexcept {
    const auto live=pearlParsePrivateExecution(getenv("CANARY_PEARL_PRIVATE_EXECUTION"),getenv("CANARY_PEARL_PRIVATE_EXECUTION_MODE"));
    if (captured!=live || live==PearlPrivateExecution::Invalid) return false;
    if (live==PearlPrivateExecution::Disabled) return true;
    if (getenv("CANARY_PEARL_INTEGRATED_PIPELINE")) return false;
    const char *selector=getenv("CANARY_PEARL_EXPERIMENT"),*overlap=getenv("CANARY_PEARL_ATTEMPT_OVERLAP");
    if (!pearlPrivateCompiled(live) ||
        (pearlPrivateGeneric(live)?selector!=nullptr:(!selector || std::strcmp(selector,"sm120-wm8-wn2-tm2-tn4-runtime-v1"))) ||
        (pearlPrivateSerial(live)?overlap!=nullptr:(!overlap || std::strcmp(overlap,pearlPrivateName(live))))) return false;
    for (const char *key:{"CANARY_PEARL_NATIVE_EVIDENCE","CANARY_PEARL_SHAPE_EXPERIMENT","CANARY_PEARL_SHAPE",
            "CANARY_PEARL_TILE","CANARY_PEARL_PROFILE_INTERVAL","CANARY_PEARL_A_ROOT_FUSION",
            "CANARY_PEARL_HARD_TARGET","CANARY_PEARL_BENCH_ALL_HIT"}) if (getenv(key)) return false;
    return true;
}
inline bool pearlPrivateSelectionValid(const PearlNativeSelection &v, PearlPrivateExecution mode=PearlPrivateExecution::Serial) noexcept {
    if (pearlPrivateGeneric(mode)) return pearlSm120NativeSelectionValid(v);
    return pearlNativeSelectionValid(v) && v.sm==120 && v.m==4096 && v.n==65536 && v.k==2048 && v.rank==128 &&
        v.workUnitsPerAttempt==1048576 && v.preparationPlanId==UINT64_C(0x4355444100010028) &&
        v.kernelId=="cuda-ptx-mma-s8-sm120-wm8-runtime-experimental-v1" && v.tileId=="wm8-wn2-tm2-tn4-bk64-st3-gm8";
}

}
