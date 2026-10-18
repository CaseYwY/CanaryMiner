#pragma once
#include "pearl_public_execution.h"
#include "pearl_sm120_native_evidence.h"
namespace om {
constexpr std::uint64_t kPearlGenericSingleAttemptBudget=256;
inline bool pearlPublicSelectionValid(const PearlNativeSelection &v,PearlPublicExecution mode) noexcept {
    return pearlPublicCompiled(mode) && pearlPublicArchitecture(mode)==120 && pearlSm120NativeSelectionValid(v);
}
inline bool pearlPublicJobValid(const Job &j,const PearlSourcePolicy &p,PearlPublicExecution mode) noexcept {
    return pearlPublicArchitecture(mode)==120 && pearlSm120NativeJobValid(j,p);
}
inline bool pearlGenericNativeEvidenceMode(const char *mode) noexcept {
    return pearlSm120NativeEvidenceMode(mode) || pearlPublicNativeMode(mode);
}
inline bool pearlGenericNativeEvidenceRequest(const char *mode,const char *selector,bool bounded) noexcept {
    return !mode || (pearlGenericNativeEvidenceMode(mode) && !selector && bounded);
}
inline const char *pearlGenericSelectionMode(const PearlNativeSelection &v) noexcept {
    if (pearlSm120NativeEvidenceMode("sm120-generic-v1") && pearlSm120NativeSelectionValid(v)) return "sm120-generic-v1";
    return nullptr;
}
inline bool pearlGenericAttemptBatch(std::uint64_t bound,std::uint64_t units,std::uint64_t work,std::uint64_t attempts) noexcept {
    return bound>=kPearlGenericSingleAttemptBudget && units==bound && work==bound && attempts==1;
}
inline bool pearlGenericWorkConserved(std::uint64_t bound,std::uint64_t work,std::uint64_t attempts) noexcept {
    return bound>=kPearlGenericSingleAttemptBudget && bound!=UINT64_MAX && attempts!=UINT64_MAX &&
        attempts<=UINT64_MAX/bound && work!=UINT64_MAX && work==attempts*bound;
}
class PearlGenericNativeBinding final {
 public:
    explicit PearlGenericNativeBinding(const char *mode) noexcept
        : sm120_(pearlSm120NativeEvidenceMode(mode)||pearlPublicNativeMode(mode)), public_(pearlPublicNativeMode(mode)) {}
    bool modeMatches(const char *mode) const noexcept { return public_?(pearlPublicNativeMode(mode) && !std::strcmp(mode,this->mode())):sm120_ && pearlSm120NativeEvidenceMode(mode); }
    const char *mode() const noexcept { return public_?kPearlPublicProfile:sm120_?"sm120-generic-v1":nullptr; }
    bool observe(const PearlNativeSelection &v) noexcept { return sm120_ && b_.observe(v); }
    bool check(const PearlNativeSelection &v,const Job &j,const PearlSourcePolicy &p) noexcept { return sm120_ && b_.check(v,j,p); }
    bool jobValid(const Job &j,const PearlSourcePolicy &p) const noexcept { return sm120_ && pearlSm120NativeJobValid(j,p); }
    bool bound() const noexcept { return sm120_ && b_.bound(); }
    const PearlNativeSelection &selection() const noexcept { return b_.selection(); }
 private:
    const bool sm120_;
    const bool public_;
    PearlSm120NativeBinding b_;
};
} // namespace om
