#pragma once
#include "pearl_sm120_native_selection.h"
#include "pearl_sm120_native_mode.h"
#include "pearl_job_context.h"
#include "../algos/pearl-pow/cuda_preparation_plan.h"
#include <array>
#include <algorithm>

namespace om {
inline bool pearlSm120NativeJobValid(const Job &job, const PearlSourcePolicy &policy) noexcept {
    PearlJobContext context;
    return policy.network == PearlNetwork::Mainnet &&
        policy.targetSemantics == PearlTargetSemantics::BaseBeforePenalty &&
        validatePearlJobContext(job,&policy,&context,nullptr) &&
        context.certificateVersion == PearlCertificateVersion::V3;
}

// This binds observed selection/work, NOT job publication or proof ownership.
// Those remain the existing PearlRunSession's responsibility. Copy string
// bytes so a mutable backend view cannot silently mutate the frozen tuple.
class PearlSm120NativeBinding final {
 public:
    bool observe(const PearlNativeSelection &view) noexcept {
        if (failed_ || !pearlSm120NativeSelectionValid(view)) return fail();
        if (bound_) return pearlNativeSelectionEqual(view,frozen_) || fail();
        if (view.kernelId.size() >= kernel_.size() || view.tileId.size() >= tile_.size()) return fail();
        std::copy(view.kernelId.begin(),view.kernelId.end(),kernel_.begin());
        std::copy(view.tileId.begin(),view.tileId.end(),tile_.begin());
        frozen_=view;
        frozen_.kernelId=std::string_view(kernel_.data(),view.kernelId.size());
        frozen_.tileId=std::string_view(tile_.data(),view.tileId.size());
        bound_=true;
        return true;
    }
    bool check(const PearlNativeSelection &view,const Job &job,
               const PearlSourcePolicy &policy) noexcept {
        return (bound_ && !failed_ && pearlSm120NativeJobValid(job,policy) &&
            pearlSm120NativeSelectionValid(view) &&
            pearlNativeSelectionEqual(view,frozen_)) || fail();
    }
    bool bound() const noexcept { return bound_ && !failed_; }
    const PearlNativeSelection &selection() const noexcept { return frozen_; }
    PearlSm120NativeBinding() = default;
    PearlSm120NativeBinding(const PearlSm120NativeBinding &) = delete;
    PearlSm120NativeBinding &operator=(const PearlSm120NativeBinding &) = delete;
 private:
    bool fail() noexcept { failed_=true; return false; }
    bool bound_=false,failed_=false;
    std::array<char,128> kernel_{},tile_{};
    PearlNativeSelection frozen_{};
};
} // namespace om
