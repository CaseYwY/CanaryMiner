// Job/catalog-independent ABI types; safe for algo.h.
#pragma once
#include <cstdint>
namespace om {
enum class PearlPrivateExecution { Disabled, Serial, APrep64, Invalid, GenericSerial, GenericAPrep };
inline constexpr const char *kPearlPrivateProfile="sm120-wm8-lookahead-private-v1";
inline constexpr const char *kPearlPrivateGenericProfile="sm120-generic-lookahead-private-v1";
inline bool pearlPrivateGeneric(PearlPrivateExecution mode) noexcept {
    return mode==PearlPrivateExecution::GenericSerial || mode==PearlPrivateExecution::GenericAPrep;
}
inline bool pearlPrivateSerial(PearlPrivateExecution mode) noexcept {
    return mode==PearlPrivateExecution::Serial || mode==PearlPrivateExecution::GenericSerial;
}
inline const char *pearlPrivateProfile(PearlPrivateExecution mode) noexcept {
    return pearlPrivateGeneric(mode)?kPearlPrivateGenericProfile:kPearlPrivateProfile;
}
inline uint64_t pearlPrivateBatch(PearlPrivateExecution mode) noexcept { return pearlPrivateGeneric(mode)?256:1048576; }
inline bool pearlPrivateEnabled(PearlPrivateExecution mode) noexcept {
    return mode==PearlPrivateExecution::Serial || mode==PearlPrivateExecution::APrep64 || pearlPrivateGeneric(mode);
}
inline const char *pearlPrivateName(PearlPrivateExecution mode) noexcept {
    return pearlPrivateSerial(mode)?"serial":mode==PearlPrivateExecution::APrep64?"a-prep64-v1":mode==PearlPrivateExecution::GenericAPrep?"a-prep-v1":"invalid";
}
struct PearlPrivateExecutionStats {
    bool valid=false, lastValid=false, cacheHit=false;
    uint64_t prepared=0,promoted=0,discarded=0,ready=0,nonce=0;
    unsigned slot=0;
    bool discardReady() noexcept {
        if (!valid || ready!=1 || discarded==UINT64_MAX) {valid=false;return false;}
        ++discarded; ready=0; return conserved();
    }
    bool finishSearch(bool ownedReady,bool cacheUsed,bool success,bool newReady) noexcept {
        if (!conserved() || ready!=(ownedReady?1U:0U) || (cacheUsed && !ownedReady)) {valid=false;return false;}
        if (ownedReady) {
            uint64_t &counter=(cacheUsed && success)?promoted:discarded;
            if (counter==UINT64_MAX) {valid=false;return false;}
            ++counter;
        }
        if (newReady) {
            if (!success || prepared==UINT64_MAX) {valid=false;return false;}
            ++prepared;
        }
        ready=newReady?1:0;
        return conserved();
    }
    bool conserved() const noexcept {
        return valid && ready<=1 && promoted<=prepared && discarded<=prepared-promoted &&
            ready==prepared-promoted-discarded;
    }
    bool terminal(PearlPrivateExecution mode,uint64_t attempts) const noexcept {
        return conserved() && !ready && promoted<=attempts &&
            (pearlPrivateSerial(mode)?(!prepared&&!promoted&&!discarded):(mode==PearlPrivateExecution::APrep64 || mode==PearlPrivateExecution::GenericAPrep));
    }
};
} // namespace om
