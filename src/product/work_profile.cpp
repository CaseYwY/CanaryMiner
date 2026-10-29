#include "work_profile.h"
#include "hash.h"
#include <algorithm>
namespace om {
namespace {
using S=WorkProfileStatus;
constexpr uint64_t day=86400000;constexpr int64_t lastUtc=253402300799999LL;
bool instant(int64_t n){return n>=0&&n<=lastUtc;}
bool key(const std::string &s){return !s.empty()&&s.size()<=256&&s.find('\0')==std::string::npos;}
bool work(const ExactDecimal &n){return !n.negative()&&n.digits()<=38&&n.scale()<=12;}
ExactDecimal integer(uint64_t n){ExactDecimal value;ExactDecimal::parse(std::to_string(n),&value);return value;}
bool cancelled(const std::atomic<bool> *c){return c&&c->load();}
bool less(const WorkProfileRate &a,const WorkProfileRate &b){ExactDecimal left,right;
    return a.completedTmacs.multiply(integer(b.elapsedMs),&left)&&b.completedTmacs.multiply(integer(a.elapsedMs),&right)&&left.compare(right)<0;}
bool energy(const WorkProfileSample &a,const WorkProfileSample &b,ElectricityAmount &total){ExactDecimal factor,watts,part;ExactDecimal::parse("0.001",&factor);
    return integer(*a.milliwatts).multiply(factor,&watts)&&watts.multiply(integer(b.monotonicMs-a.monotonicMs),&part)&&total.numerator.add(part,&total.numerator);}
}
std::string workProfileFingerprint(const WorkProfileIdentity &i){
    if(i.power.basis<PowerBasis::GpuBoard||i.power.basis>PowerBasis::WholeMeterImport||i.power.origin<PowerOrigin::SyntheticFixture||i.power.origin>PowerOrigin::DeclaredForecast)return {};
    std::string bytes="CMWP1|"+std::to_string(static_cast<unsigned>(i.power.basis))+"|"+std::to_string(static_cast<unsigned>(i.power.origin))+"|host-wait-complete-batch|";
    for(const auto *s:{&i.deviceKey,&i.platformKey,&i.softwareVersion,&i.driverVersion,&i.kernelKey,&i.workShapeKey,&i.environmentKey,&i.power.scope,&i.power.adapter}){
        if(!key(*s))return {};
        bytes+=std::to_string(s->size())+":"+*s;
    }
    return sha256Hex(bytes);
}
S validateWorkProfilePlan(const WorkProfilePlan &p){
    if(!p.enabled)return S::Disabled;
    if(p.qualification!=WorkProfileQualification::SyntheticFixture||p.identity.power.origin!=PowerOrigin::SyntheticFixture)return S::Unsupported;
    if(!key(p.key)||p.key.rfind("fixture.",0)!=0||workProfileFingerprint(p.identity).empty()||p.hostWaitMs.empty()||p.hostWaitMs.size()>32||
        !p.repetitions||p.repetitions>32||!p.telemetryCadenceMs||p.telemetryCadenceMs>60000||p.maximumSampleGapMs<p.telemetryCadenceMs||p.maximumSampleGapMs>60000||
        p.warmupMs>3600000||p.measurementMs<p.telemetryCadenceMs||p.measurementMs>3600000||!p.maximumElapsedMs||p.maximumElapsedMs>day||
        !p.validForMs||p.validForMs>30*day||p.maximumEnergy.numerator.negative()||p.maximumEnergy.numerator.zero()||p.maximumEnergy.numerator.digits()>38||p.maximumEnergy.numerator.scale()>12)return S::Invalid;
    for(size_t i=0;i<p.hostWaitMs.size();++i)if(p.hostWaitMs[i]>1000||std::find(p.hostWaitMs.begin(),p.hostWaitMs.begin()+i,p.hostWaitMs[i])!=p.hostWaitMs.begin()+i)return S::Invalid;
    return (p.warmupMs+p.measurementMs)*p.repetitions*p.hostWaitMs.size()>p.maximumElapsedMs?S::Budget:S::Ok;
}
std::string workProfilePlanFingerprint(const WorkProfilePlan &p){const auto identity=workProfileFingerprint(p.identity);
    if(identity.empty()||!key(p.key)||p.hostWaitMs.empty()||p.hostWaitMs.size()>32)return {};
    std::string bytes="CMWPP1|"+identity+"|"+std::to_string(p.key.size())+":"+p.key;
    for(uint64_t n:{static_cast<uint64_t>(p.enabled),static_cast<uint64_t>(p.qualification),static_cast<uint64_t>(p.telemetryCadenceMs),static_cast<uint64_t>(p.maximumSampleGapMs),
        p.warmupMs,p.measurementMs,p.maximumElapsedMs,p.validForMs,static_cast<uint64_t>(p.repetitions)})bytes+="|"+std::to_string(n);
    bytes+="|"+p.maximumEnergy.numerator.text()+"|waits";for(auto wait:p.hostWaitMs)bytes+="|"+std::to_string(wait);return sha256Hex(bytes);
}
bool WorkProfileRate::formatPerSecond(unsigned places,std::string *out)const{ExactDecimal n;
    return elapsedMs&&completedTmacs.multiply(integer(1000),&n)&&n.formatRatio(integer(elapsedMs),places,out);}
std::shared_ptr<const WorkProfile> buildWorkProfile(const WorkProfileInput &in,S *status,const std::atomic<bool> *cancel){
    auto fail=[&](S s)->std::shared_ptr<const WorkProfile>{if(status)*status=s;return {};};
    if(cancelled(cancel))return fail(S::Cancelled);
    const auto valid=validateWorkProfilePlan(in.plan);if(valid!=S::Ok)return fail(valid);
    if(!in.generation||!in.counterEpoch||in.samples.size()<2||in.windows.size()!=in.plan.hostWaitMs.size()*in.plan.repetitions)return fail(S::Invalid);
    if(in.samples.size()>8192)return fail(S::Capacity);
    try{const auto fingerprint=workProfileFingerprint(in.plan.identity);const auto &first=in.samples.front();const auto &last=in.samples.back();
        if(!first.completeWorkBoundary||!last.completeWorkBoundary)return fail(S::Coverage);
        WorkProfileSummary result;result.sampleCount=in.samples.size();result.points.resize(in.plan.hostWaitMs.size());
        std::vector<ElectricityAmount> prefix(in.samples.size());
        for(size_t i=0;i<in.samples.size();++i){if(cancelled(cancel))return fail(S::Cancelled);const auto &s=in.samples[i];
            if(s.generation!=in.generation||s.counterEpoch!=in.counterEpoch||s.fingerprint!=fingerprint)return fail(S::Attribution);
            if(!instant(s.utcMs))return fail(S::Clock);
            if(s.fault||!s.safetyTelemetryValid||!s.thermalSafe)return fail(S::Unsafe);
            if(!s.gameStateKnown||s.gameActive)return fail(S::Game);
            if(!s.milliwatts||!s.userTmacs||!s.donationTmacs)return fail(S::Coverage);
            if(!work(*s.userTmacs)||!work(*s.donationTmacs)||s.appliedHostWaitMs>1000)return fail(S::Invalid);
            if(!i)continue;
            const auto &previous=in.samples[i-1];
            if(s.monotonicMs<=previous.monotonicMs||s.utcMs<=previous.utcMs||static_cast<uint64_t>(s.utcMs-previous.utcMs)!=s.monotonicMs-previous.monotonicMs)return fail(S::Clock);
            if(s.monotonicMs-previous.monotonicMs>in.plan.maximumSampleGapMs)return fail(S::Coverage);
            if(s.userTmacs->compare(*previous.userTmacs)<0||s.donationTmacs->compare(*previous.donationTmacs)<0)return fail(S::Attribution);
            prefix[i]=prefix[i-1];if(!energy(previous,s,prefix[i]))return fail(S::Capacity);
            if(s.monotonicMs-first.monotonicMs>in.plan.maximumElapsedMs||prefix[i].numerator.compare(in.plan.maximumEnergy.numerator)>0)return fail(S::Budget);
        }
        result.beginUtcMs=first.utcMs;result.endUtcMs=last.utcMs;result.elapsedMs=last.monotonicMs-first.monotonicMs;result.totalEnergy=prefix.back();
        if(!last.userTmacs->subtract(*first.userTmacs,&result.totalUserTmacs)||!last.donationTmacs->subtract(*first.donationTmacs,&result.totalDonationTmacs))return fail(S::Capacity);
        ExactDecimal measuredUser,measuredDonation;
        for(size_t n=0;n<in.windows.size();++n){if(cancelled(cancel))return fail(S::Cancelled);const auto &w=in.windows[n];
            if(w.candidate!=n/in.plan.repetitions||w.repetition!=n%in.plan.repetitions||w.beginSample>w.measurementSample||w.measurementSample>=w.endSample||w.endSample>=in.samples.size()||
                (n&&w.beginSample<in.windows[n-1].endSample))return fail(S::Invalid);
            const auto &a=in.samples[w.beginSample],&b=in.samples[w.measurementSample],&c=in.samples[w.endSample];
            if(!a.completeWorkBoundary||!b.completeWorkBoundary||!c.completeWorkBoundary)return fail(S::Coverage);
            WorkProfileTrial t;t.candidate=w.candidate;t.repetition=w.repetition;t.beginUtcMs=b.utcMs;t.endUtcMs=c.utcMs;
            t.warmupMs=b.monotonicMs-a.monotonicMs;t.elapsedMs=c.monotonicMs-b.monotonicMs;
            if(t.warmupMs<in.plan.warmupMs||t.warmupMs>in.plan.warmupMs+in.plan.maximumSampleGapMs||(!in.plan.warmupMs&&w.beginSample!=w.measurementSample)||
                t.elapsedMs<in.plan.measurementMs||t.elapsedMs>in.plan.measurementMs+in.plan.maximumSampleGapMs)return fail(S::Coverage);
            for(size_t i=w.beginSample;i<=w.endSample;++i)if(in.samples[i].appliedHostWaitMs!=in.plan.hostWaitMs[w.candidate])return fail(S::Attribution);
            if(!c.userTmacs->subtract(*b.userTmacs,&t.userTmacs)||!c.donationTmacs->subtract(*b.donationTmacs,&t.donationTmacs)||
                !prefix[w.endSample].numerator.subtract(prefix[w.measurementSample].numerator,&t.energy.numerator))return fail(S::Capacity);
            auto &p=result.points[w.candidate];p.hostWaitMs=in.plan.hostWaitMs[w.candidate];p.measuredMs+=t.elapsedMs;
            if(!p.userTmacs.add(t.userTmacs,&p.userTmacs)||!p.donationTmacs.add(t.donationTmacs,&p.donationTmacs)||!p.energy.numerator.add(t.energy.numerator,&p.energy.numerator)||
                !result.measuredEnergy.numerator.add(t.energy.numerator,&result.measuredEnergy.numerator)||!measuredUser.add(t.userTmacs,&measuredUser)||!measuredDonation.add(t.donationTmacs,&measuredDonation))return fail(S::Capacity);
            const WorkProfileRate rate{t.userTmacs,t.elapsedMs};
            if(!p.repetitions||less(rate,p.minimumUserRate))p.minimumUserRate=rate;
            if(!p.repetitions||less(p.maximumUserRate,rate))p.maximumUserRate=rate;
            ++p.repetitions;result.measuredMs+=t.elapsedMs;result.trials.push_back(std::move(t));
        }
        result.overheadMs=result.elapsedMs-result.measuredMs;
        if(!result.totalEnergy.numerator.subtract(result.measuredEnergy.numerator,&result.overheadEnergy.numerator)||
            !result.totalUserTmacs.subtract(measuredUser,&result.overheadUserTmacs)||!result.totalDonationTmacs.subtract(measuredDonation,&result.overheadDonationTmacs))return fail(S::Capacity);
        result.completeCoverage=true;
        auto profile=std::shared_ptr<const WorkProfile>(new WorkProfile(in,std::move(result)));if(cancelled(cancel))return fail(S::Cancelled);
        if(status)*status=S::Ok;
        return profile;
    }catch(...){return fail(S::Capacity);}
}
S WorkProfileUse::check(const WorkProfilePlan &plan,int64_t now,const WorkProfileConditions &c){
    if(!intent_.permits(token_))return latched_=S::Stopped;
    if(latched_!=S::Ok)return latched_;
    if(!profile_)return latched_=S::Invalid;
    if(c.fault)return latched_=S::Unsafe;
    if(!instant(now)||now<profile_->summary().endUtcMs||(lastUtcMs_&&now<*lastUtcMs_))return latched_=S::Clock;
    lastUtcMs_=now;
    if(!plan.enabled)return latched_=S::Disabled;
    try{if(workProfilePlanFingerprint(plan)!=workProfilePlanFingerprint(profile_->input().plan))return latched_=S::Attribution;}
    catch(...){return latched_=S::Capacity;}
    if(c.drift)return latched_=S::Drift;
    if(static_cast<uint64_t>(now-profile_->summary().endUtcMs)>profile_->input().plan.validForMs)return latched_=S::Stale;
    if(!c.safetyTelemetryValid||!c.thermalSafe)return S::Unsafe;
    if(!c.gameStateKnown||c.gameActive)return S::Game;
    return S::Ok;
}
} // namespace om
