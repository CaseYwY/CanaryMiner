// Memory-only adapters; callbacks are valid for the owning runtime's lifetime, including after revocation.
#pragma once
#include "energy_runtime.h"
#include "energy_review.h"
#include "energy_authorization_settings.h"
namespace om {
inline EnergyReviewAccess energyRuntimeReview(EnergyRuntime &runtime){
    EnergyReviewAccess a;a.snapshot=[&runtime]{return runtime.snapshot();};
    a.inspect=[&runtime](uint64_t g,uint64_t r){return runtime.inspect(g,r);};
    a.retry=[&runtime](uint64_t g,uint64_t r){return runtime.retryReviewed(g,r);};return a;
}
inline EnergyAuthorizationAccess energyRuntimeAuthorization(EnergyRuntime &runtime){
    EnergyAuthorizationAccess a;a.snapshot=[&runtime]{return runtime.authorizationSnapshot();};
    a.submit=[&runtime](uint64_t g,uint64_t r,const EnergyAuthorizationCommand &c){return runtime.authorize(g,r,c);};
    a.acknowledge=[&runtime](uint64_t g,uint64_t r){return runtime.acknowledge(g,r);};
    a.captureSnapshot=[&runtime]{return runtime.snapshot();};a.selectedDevices=runtime.selectedDevices();return a;
}
} // namespace om
