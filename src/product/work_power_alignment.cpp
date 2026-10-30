#include "work_power_alignment.h"
namespace om {
namespace {
using R=WorkProfileStatus;
bool key(const std::string &s){if(s.size()!=32)return false;for(char c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
bool text(const std::string &s){return !s.empty()&&s.size()<=256&&s.find('\0')==std::string::npos;}
bool valid(CompletedWorkTime t){return t.utcMs>=0&&t.utcMs<=253402300799999LL&&t.monotonicMs;}
bool ordered(CompletedWorkTime a,CompletedWorkTime b){return b.utcMs>=a.utcMs&&b.monotonicMs>=a.monotonicMs&&
    static_cast<uint64_t>(b.utcMs-a.utcMs)==b.monotonicMs-a.monotonicMs;}
bool inside(CompletedWorkTime a,CompletedWorkTime b,CompletedWorkTime t){return t.utcMs>a.utcMs&&t.utcMs<b.utcMs&&
    t.monotonicMs>a.monotonicMs&&t.monotonicMs<b.monotonicMs;}
bool amount(const ExactDecimal &n){return !n.negative()&&n.digits()<=38&&n.scale()<=12;}
bool same(const PowerEvidence &a,const PowerEvidence &b){return a.basis==b.basis&&a.origin==b.origin&&a.scope==b.scope&&a.adapter==b.adapter;}
}
std::shared_ptr<const AlignedWorkPower> alignWorkPower(const WorkPowerAlignmentPolicy &p,const CompletedWorkInterval &w,
    const WorkPowerReading &s,CompletedWorkTime now,R *status){
    const auto fail=[&](R r)->std::shared_ptr<const AlignedWorkPower>{if(status)*status=r;return {};};
    if(!p.enabled)return fail(R::Disabled);
    if(!key(p.workSourceKey)||!p.generation||!p.counterEpoch||!p.maximumBracketMs||p.maximumBracketMs>60000||
        !p.maximumReadMs||p.maximumReadMs>60000||!p.maximumSourceAgeMs||p.maximumSourceAgeMs>60000||
        !text(p.evidence.scope)||!text(p.evidence.adapter))return fail(R::Invalid);
    if(p.evidence.basis<PowerBasis::GpuBoard||p.evidence.basis>PowerBasis::WholeMeterImport||
        (p.evidence.origin!=PowerOrigin::SyntheticFixture&&p.evidence.origin!=PowerOrigin::ObservedTelemetry))return fail(R::Unsupported);
    if(w.sourceKey!=p.workSourceKey||s.workSourceKey!=p.workSourceKey||w.generation!=p.generation||s.generation!=p.generation||
        w.counterEpoch!=p.counterEpoch||s.counterEpoch!=p.counterEpoch||!same(p.evidence,s.evidence))return fail(R::Attribution);
    if(!w.run||!w.number||!w.beginEvent||w.beginEvent==UINT64_MAX||w.endEvent!=w.beginEvent+1||!s.sequence||w.appliedHostWaitMs>1000||
        (w.lane!=WorkLane::User&&w.lane!=WorkLane::Donation)||(w.kind!=CompletedWorkBoundaryKind::Paused&&w.kind!=CompletedWorkBoundaryKind::Runnable)||
        !amount(w.userTmacs)||!amount(w.donationTmacs))return fail(R::Invalid);
    if(!s.milliwatts||!s.support)return fail(R::Coverage);
    const auto support=*s.support;
    for(const auto t:{w.begin,w.end,s.observed,s.readBegin,s.readEnd,support.begin,support.end,now})if(!valid(t))return fail(R::Clock);
    if(!ordered(w.begin,w.end)||w.end.monotonicMs==w.begin.monotonicMs||!ordered(s.readBegin,s.readEnd)||!ordered(support.begin,support.end)||
        !ordered(support.begin,s.observed)||!ordered(s.observed,support.end)||!ordered(support.end,s.readEnd)||!ordered(w.end,now)||
        !ordered(w.begin,s.observed)||!ordered(w.begin,s.readBegin)||!ordered(w.begin,support.begin))return fail(R::Clock);
    // Strict containment excludes unresolved same-millisecond order at either work-boundary edge.
    for(const auto t:{s.observed,s.readBegin,s.readEnd,support.begin,support.end})if(!inside(w.begin,w.end,t))return fail(R::Coverage);
    if(w.end.monotonicMs-w.begin.monotonicMs>p.maximumBracketMs||s.readEnd.monotonicMs-s.readBegin.monotonicMs>p.maximumReadMs)return fail(R::Coverage);
    if(now.monotonicMs-s.observed.monotonicMs>p.maximumSourceAgeMs)return fail(R::Stale);
    try{auto out=std::make_shared<const AlignedWorkPower>(AlignedWorkPower{w,s});if(status)*status=R::Ok;return out;}
    catch(...){return fail(R::Capacity);}
}
} // namespace om
