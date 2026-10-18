#pragma once
#include "pearl_private_execution_types.h"
#include <cstdlib>
#include <cstring>
#include <initializer_list>
namespace om {
enum class PearlPublicExecution { Disabled, Serial, APrep, Invalid };
inline constexpr const char *kPearlPublicProfile="sm120-generic-lookahead-public-v1";
inline unsigned pearlPublicArchitecture(PearlPublicExecution v) noexcept {
    return v==PearlPublicExecution::Serial || v==PearlPublicExecution::APrep?120:0;
}
inline bool pearlPublicSerial(PearlPublicExecution v) noexcept { return v==PearlPublicExecution::Serial; }
inline const char *pearlPublicProfile(PearlPublicExecution v) noexcept { (void)v; return kPearlPublicProfile; }
inline bool pearlPublicEnabled(PearlPublicExecution v) noexcept { return pearlPublicArchitecture(v)!=0; }
inline const char *pearlPublicName(PearlPublicExecution v) noexcept { return pearlPublicSerial(v)?"serial":pearlPublicEnabled(v)?"a-prep-v1":"invalid"; }
// Reuse arithmetic only. This never admits private environment or labels public evidence private.
inline PearlPrivateExecution pearlPublicAccountingMode(PearlPublicExecution v) noexcept { return pearlPublicSerial(v)?PearlPrivateExecution::GenericSerial:PearlPrivateExecution::GenericAPrep; }
inline bool pearlPublicCompiled(PearlPublicExecution mode=PearlPublicExecution::Serial) noexcept {
    if(pearlPublicArchitecture(mode)!=120)return false;
#if defined(CANARY_PEARL_ENABLE_PUBLIC_GENERIC_LOOKAHEAD) && CANARY_PEARL_ENABLE_PUBLIC_GENERIC_LOOKAHEAD == 1 && defined(CANARY_PEARL_ONLY_BUILD) && CANARY_PEARL_ONLY_BUILD == 1 && defined(CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT) && CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT == 1 && defined(CANARY_PEARL_ENABLE_SM120_NATIVE_EVIDENCE) && CANARY_PEARL_ENABLE_SM120_NATIVE_EVIDENCE == 1 && defined(CANARY_PEARL_ENABLE_ATTEMPT_OVERLAP) && CANARY_PEARL_ENABLE_ATTEMPT_OVERLAP == 1
    return true;
#else
    return false;
#endif
}
inline bool pearlPublicNativeMode(const char *mode) noexcept { return mode && !std::strcmp(mode,kPearlPublicProfile) && pearlPublicCompiled(); }
inline PearlPublicExecution pearlParsePublicExecution(const char *mode,const char *execution) noexcept {
    const bool named=mode && !std::strcmp(mode,kPearlPublicProfile);
    if (!named && !execution) return PearlPublicExecution::Disabled;
    if (!named || !execution) return PearlPublicExecution::Invalid;
    return !std::strcmp(execution,"serial")?PearlPublicExecution::Serial:!std::strcmp(execution,"a-prep-v1")?PearlPublicExecution::APrep:PearlPublicExecution::Invalid;
}
inline bool pearlPublicEnvironment(PearlPublicExecution captured) noexcept {
    const auto live=pearlParsePublicExecution(getenv("CANARY_PEARL_NATIVE_EVIDENCE"),getenv("CANARY_PEARL_PUBLIC_EXECUTION"));
    if(live!=captured || live==PearlPublicExecution::Invalid)return false;
    if(!pearlPublicEnabled(live))return true;
    if(getenv("CANARY_PEARL_INTEGRATED_PIPELINE"))return false;
    if(!pearlPublicCompiled(live))return false;
    for(const char *k:{"CANARY_PEARL_PRIVATE_EXECUTION","CANARY_PEARL_PRIVATE_EXECUTION_MODE","CANARY_PEARL_EXPERIMENT","CANARY_PEARL_SHAPE_EXPERIMENT","CANARY_PEARL_SHAPE","CANARY_PEARL_TILE","CANARY_PEARL_PROFILE_INTERVAL","CANARY_PEARL_A_ROOT_FUSION","CANARY_PEARL_HARD_TARGET","CANARY_PEARL_BENCH_ALL_HIT"})if(getenv(k))return false;
    const char *overlap=getenv("CANARY_PEARL_ATTEMPT_OVERLAP");
    return pearlPublicSerial(live)?!overlap:overlap && !std::strcmp(overlap,"a-prep-v1");
}
inline bool pearlPublicPolicy(PearlPublicExecution mode,bool ownedLoopback,uint64_t cap,uint64_t batch,unsigned quota,unsigned seconds,bool sustainedPublic) noexcept {
    if(!pearlPublicEnabled(mode))return mode==PearlPublicExecution::Disabled;
    return pearlPublicEnvironment(mode) && ownedLoopback && batch==256 &&
        ((quota==3 && !seconds && !sustainedPublic && cap==20) || (!quota && seconds==7200 && sustainedPublic && cap==256) ||
         (!quota && seconds==86400 && sustainedPublic && cap==4096 && mode==PearlPublicExecution::Serial &&
          getenv("CANARY_PEARL_SUSTAINED_MODE") && !std::strcmp(getenv("CANARY_PEARL_SUSTAINED_MODE"),"public-24h-v1")));
}
}
