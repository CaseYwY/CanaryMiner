// Borrowed owner must outlive the panel/session. Stop only revokes; the owning caller joins outside rendering/input.
#pragma once
#include "billing_review.h"
namespace om {
inline BillingReviewAccess billingOwnerReview(BillingOwner &owner){
    BillingReviewAccess a;a.snapshot=[&owner]{return owner.snapshot();};a.inspect=[&owner](uint64_t g,uint64_t r){return owner.refresh(g,r);};
    a.create=[&owner](uint64_t g,uint64_t r){return owner.createReviewed(g,r);};a.commit=[&owner](uint64_t g,uint64_t r){return owner.commitReviewed(g,r);};
    a.retry=[&owner](uint64_t g,uint64_t r){return owner.retryReviewed(g,r);};a.reconcile=[&owner](uint64_t g,uint64_t r){return owner.reconcileReviewed(g,r);};
    a.requestStop=[&owner]{owner.requestStop();};return a;
}
// Fixed lock order at final handoff: monetary admission -> compute claim. The compute owner never calls back into money.
inline BillingReviewAccess billingCandidateReview(BillingOwner &owner,BillingComputeOwner &compute){
    auto a=billingOwnerReview(owner);
    a.candidateSnapshot=[&owner,&compute]{const auto m=owner.snapshot();if(m&&m->generation)compute.observe(m->generation,m);else compute.requestStop();return compute.snapshot();};
    a.recompute=[&owner,&compute](uint64_t g,uint64_t r){const auto m=owner.snapshot();if(m&&m->generation)compute.observe(m->generation,m);return compute.refresh(g,r);};
    a.submit=[&owner,&compute](uint64_t g,uint64_t r,uint64_t mg,uint64_t mr){
        const auto m=owner.snapshot();if(m&&m->generation)compute.observe(m->generation,m);
        const auto c=compute.snapshot();
        if(!billingCandidateMatches(m,c)||c->generation!=g||c->revision!=r||m->generation!=mg||m->revision!=mr)return BillingOwnerAdmission::ReviewChanged;
        return owner.submitReviewed(mg,mr,c->candidate->original,[&compute,g,r,mg,mr]{return compute.claimReviewed(g,r,mg,mr);});
    };
    a.requestStop=[&owner,&compute]{owner.requestStop();compute.requestStop();};return a;
}
}
