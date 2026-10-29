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
}
