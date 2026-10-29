// Complete private candidate review; hypothetical arithmetic is never labelled committed money.
#pragma once
#include "billing_compute_owner.h"
#include "billing_review_document.h"
namespace om {
inline bool billingCandidateMatches(const std::shared_ptr<const BillingOwnerSnapshot> &m,const std::shared_ptr<const BillingComputeSnapshot> &c){
    if(!m||!c||!c->generation||!c->revision||!c->reviewable||c->pending||c->state!=BillingComputeState::Ready||!c->candidate||
        c->monetary!=m||c->candidate->reviewed!=m||c->generation!=m->generation||!m->revision||m->state!=BillingOwnerState::Ready||m->pending||!m->reviewable||m->uncertain||
        !m->context||!m->context->binding.source||!m->view||!m->view->stable||!m->view->stage||!m->view->monetary)return false;
    const auto &stage=*m->view->stage;const auto &money=*m->view->monetary;
    return stage.status==BillingStatus::Ok&&!stage.replacementPending&&(!stage.present||stage.phase==BillingInputPhase::Complete)&&money.status==BillingStatus::Ok&&
        money.committed.status==BillingStatus::Ok&&money.committed.source&&!money.committed.settledTotal&&
        (money.state==BillingRecoveryState::Empty||money.state==BillingRecoveryState::Confirmed||money.state==BillingRecoveryState::Rejected)&&
        c->candidate->original.receipt&&c->candidate->preview.status==ElectricityStatus::Ok&&c->candidate->preview.ending&&c->candidate->preview.provisionalTotal;
}
namespace billing_review_detail {
inline Document candidateDocument(const BillingComputeSnapshot &s){
    Document d;const auto &c=s.candidate;
    d.add("PRIVATE CANDIDATE / HYPOTHETICAL ONLY - never current money, income, profit or a payable bill.");
    d.add("Strings are quoted ASCII bytes; \\xHH preserves controls/non-ASCII. Times are UTC ms.");
    d.add("Energy/charge values are exact numerator / 3600000000; tariff rates and watts are decimal.");
    d.add("Billing status: 0 Ok,1 Replay,2 Invalid,3 Unsupported,4 Attribution,5 Conflict,6 SequenceGap,7 Clock,8 Capacity,9 Calculation,10 Storage.");
    d.add("Energy status: 0 Ok,1 Replay,2 Invalid,3 Attribution,4 Conflict,5 SequenceGap,6 Clock,7 Capacity,8 Storage,9 DetailExpired.");
    d.add("Energy recovery: 0 Disabled,1 Empty,2 Prepared,3 Confirmed,4 Rejected,5 Diverged,6 Unavailable; query mode: 0 volatile,1 inspect,2 submit,3 retry.");
    d.add("Energy open mode: 0 existing,1 create,2 readonly existing.");
    if(!c||!c->reviewed||!c->reviewed->context||!c->reviewed->view||!c->reviewed->view->stable||!c->reviewed->view->stage||!c->reviewed->view->monetary){d.add("No complete frozen candidate context.");d.complete=false;return d;}
    d.n("compute.generation",s.generation);d.n("compute.revision",s.revision);d.n("candidate.monetaryGeneration",c->reviewed->generation);d.n("candidate.monetaryRevision",c->reviewed->revision);
    d.add("PREVIEW / next prefix if this exact original is explicitly submitted and confirmed");
    d.string("preview.currency",c->reviewed->context->binding.plan.currency);d.n("preview.status (0 Ok)",c->preview.status);
    d.amount("preview.provisional change (not incremental mining cost)",c->preview.provisionalChange);
    d.amount("preview.provisional total (not final payable bill)",c->preview.provisionalTotal);d.amount("preview.settled total (still hypothetical)",c->preview.settledTotal);
    d.n("preview.settlementDependent",c->preview.settlementDependent);
    if(c->preview.ending){const auto &o=*c->preview.ending;const std::string k="preview.ending";
        d.n(k+".declared",o.declared);d.string(k+".planKey",o.planKey);d.string(k+".timeDatabaseVersion",o.timeDatabaseVersion);d.evidence(k+".evidence",o.evidence);
        d.n(k+".cycleBegin UTC ms",o.cycleBeginUtcMs);d.n(k+".cycleEnd UTC ms",o.cycleEndUtcMs);d.n(k+".through UTC ms",o.throughUtcMs);
        d.amount(k+".imported kWh",o.importedKwh);d.amount(k+".accrued subtotal",o.accruedSubtotal);
    }else d.add("preview.ending UNAVAILABLE");
    d.add("SCHEDULING HINT / not a persisted checkpoint or coverage proof");d.n("notice.sequence",c->notice.checkpoint.sequence);d.string("notice.digest",c->notice.checkpoint.digest);
    d.n("notice.through UTC ms",c->notice.throughUtcMs);d.n("notice.observed UTC ms",c->notice.observedUtcMs);d.n("candidate.originalQuery UTC ms",c->queriedUtcMs);
    d.add("FROZEN MONETARY BASIS / an earlier reviewed observation, not a fresh current read");d.n("basis.observed UTC ms",c->reviewed->view->observedUtcMs);
    d.binding(*c->reviewed->context);d.monetary("basis.monetary",*c->reviewed->view->monetary);
    const auto &stage=*c->reviewed->view->stage;d.n("basis.stage.present",stage.present);d.n("basis.stage.replacementPending",stage.replacementPending);d.n("basis.stage.status",stage.status);
    if(stage.present){d.n("basis.stage.phase",stage.phase);d.string("basis.stage.revision",stage.revision);d.n("basis.stage.receivedBytes",stage.received);d.manifest("basis.stage.manifest",stage.manifest);
        if(stage.original)d.chunk("basis.stage.original",*stage.original);else d.string("basis.stage.originalPrefix",stage.originalPrefix);
        if(stage.certificate){const auto &v=*stage.certificate;d.n("basis.stage.certificate.state",v.state);d.n("basis.stage.certificate.result",v.result);d.string("basis.stage.certificate.revision",v.revision);d.string("basis.stage.certificate.digest",v.digest);d.field("basis.stage.certificate.applied",v.applied?number(*v.applied):"NOT ATTEMPTED");d.string("basis.stage.certificate.canonicalState",v.committed);}
        if(stage.outcome)d.monetary("HISTORICAL basis.stage.outcome",*stage.outcome);}
    d.chunk("candidate.original",c->original);
    if(d.complete)d.add("END OF COMPLETE PRIVATE CANDIDATE DOCUMENT");
    return d;
}
} // namespace billing_review_detail
} // namespace om
