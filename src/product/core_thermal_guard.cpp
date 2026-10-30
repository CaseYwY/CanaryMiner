#include "core_thermal_guard.h"
#include <algorithm>
namespace om {
namespace {
using S=CoreThermalState;using O=SafetyTelemetryOwnerState;using T=LocalTaskStatus;using R=SafetyTelemetryStatus;
bool instant(GameGuardTime t){return t.utcMs>0&&t.utcMs<=253402300799999LL&&t.monotonicMs;}
bool ordered(GameGuardTime a,GameGuardTime b){return a.utcMs<=b.utcMs&&a.monotonicMs<=b.monotonicMs;}
bool aligned(GameGuardTime a,GameGuardTime b){
    if(!instant(b)||!ordered(a,b))return false;
    const auto wall=static_cast<uint64_t>(b.utcMs-a.utcMs),mono=b.monotonicMs-a.monotonicMs;
    return (wall>mono?wall-mono:mono-wall)<=2000;
}
}
bool validCoreThermalPolicy(const CoreThermalPolicy &p){
    if(p.devices.empty()||p.devices.size()>safetyTelemetryDeviceLimit||p.maximumAgeMs<100||p.maximumAgeMs>5000||p.maximumGapMs<100||p.maximumGapMs>p.maximumAgeMs||
        p.coolingMs<100||p.coolingMs>600000||p.minimumCoolSamples<2||p.minimumCoolSamples>10000)return false;
    SafetyTelemetryRequest request{true,1,1,{1,1},25,{},p.profile};
    for(const auto &device:p.devices){
        if(device.pauseAtC<=-273||device.pauseAtC>250||device.resumeAtOrBelowC< -273||device.resumeAtOrBelowC>=device.pauseAtC)return false;
        if(p.profile==SafetyTelemetryProfile::ExtendedFields){
            if(device.memoryPauseAtC<=-273||device.memoryPauseAtC>250||device.memoryResumeAtOrBelowC< -273||device.memoryResumeAtOrBelowC>=device.memoryPauseAtC)return false;
        }else if(device.memoryPauseAtC||device.memoryResumeAtOrBelowC)return false; // never silently ignore requested memory protection
        request.devices.push_back(device.uuid);
    }
    return validSafetyTelemetryRequest(request);
}
CoreThermalGuard::CoreThermalGuard(CoreThermalPolicy p,const SessionIntent &i,SessionToken token):policy_(std::move(p)),intent_(i),token_(token){}
void CoreThermalGuard::hold(S state){view_.state=state;view_.validUntilMonoMs=0;view_.coolSamples=0;view_.coolingSince.reset();admitted_=false;previousGood_.reset();}
void CoreThermalGuard::halt(S state){hold(state);terminal_=true;}
void CoreThermalGuard::requestStop(){attempted_=true;halt(S::Stopped);}
bool CoreThermalGuard::time(GameGuardTime now){
    if(!instant(now)||!ordered(last_,now)||!aligned(anchor_,now)){halt(S::ClockFault);return false;}last_=now;return true;
}
bool CoreThermalGuard::start(GameGuardTime now){
    if(attempted_)return false;
    attempted_=true;
    if(!intent_.permits(token_)){requestStop();return false;}
    if(!instant(now)){halt(S::ClockFault);return false;}
    anchor_=last_=now;
    if(!policy_.enabled){halt(S::Disabled);return false;}
    if(!validCoreThermalPolicy(policy_)){halt(S::InvalidPolicy);return false;}
    hold(S::Unavailable);return true;
}
const CoreThermalSnapshot &CoreThermalGuard::update(GameGuardTime now,const SafetyTelemetrySnapshot &source){
    view_.sourceUnreaped=view_.sourceUnreaped||source.unreaped||source.transport==T::Unreaped;
    if(!intent_.permits(token_)){requestStop();return view_;}
    if(!attempted_||terminal_)return view_;
    if(!time(now))return view_;
    if(source.unreaped||source.transport==T::Unreaped||source.state==O::Faulted||source.state==O::Stopped){halt(S::Faulted);return view_;}
    if(source.state==O::ClockFault){halt(S::ClockFault);return view_;}
    if(static_cast<unsigned>(source.state)>static_cast<unsigned>(O::Faulted)||static_cast<unsigned>(source.transport)>static_cast<unsigned>(T::Unreaped)){
        halt(S::Faulted);return view_;}
    if(!source.request){
        if(source.completed||source.reply||previousRequest_||source.state==O::Ready)halt(S::Faulted);else hold(S::Unavailable);
        return view_;
    }
    const auto &r=*source.request;std::string request,receipt;
    if(!encodeSafetyTelemetryRequest(r,1,&request)||!r.enabled||r.profile!=policy_.profile||r.epoch!=token_.epoch||source.completed!=r.revision||r.devices.size()!=policy_.devices.size()){
        halt(S::Faulted);return view_;}
    for(size_t i=0;i<r.devices.size();++i)if(r.devices[i]!=policy_.devices[i].uuid){halt(S::Faulted);return view_;}
    if(!aligned(anchor_,r.issued)||!ordered(r.issued,now)){halt(S::ClockFault);return view_;}
    if(source.reply){
        if(source.transport!=T::Ok||source.status!=source.reply->status||!encodeSafetyTelemetryReply(r,*source.reply,&receipt)||
            source.state!=(source.reply->status==R::Ok?O::Ready:O::Waiting)){halt(S::Faulted);return view_;}
    }else if(source.transport==T::Ok||source.transport==T::Invalid||source.transport==T::Unreaped||source.state!=O::Waiting||source.status!=R::Unavailable){
        halt(S::Faulted);return view_;}
    receipt=request+std::to_string(static_cast<unsigned>(source.transport))+"\n"+receipt;
    const bool fresh=!previousRequest_||r.revision!=previousRequest_->revision;
    const bool contiguous=previousRequest_&&previousRequest_->revision!=UINT64_MAX&&r.revision==previousRequest_->revision+1;
    if(previousRequest_&&(r.revision<previousRequest_->revision||(!fresh&&receipt!=previousReceipt_)||
        r.collectionBudgetMs!=previousRequest_->collectionBudgetMs||r.profile!=previousRequest_->profile)){halt(S::Faulted);return view_;}
    if(fresh&&previousRequest_&&(!ordered(previousRequest_->issued,r.issued)||r.issued.monotonicMs==previousRequest_->issued.monotonicMs)){
        halt(S::ClockFault);return view_;}
    if(fresh&&previousFinished_&&!ordered(*previousFinished_,r.issued)){halt(S::ClockFault);return view_;}
    if(fresh){previousRequest_=r;previousReceipt_=std::move(receipt);view_.sourceRevision=r.revision;sourceDeadline_=0;}
    if(!source.reply){hold(S::Unavailable);return view_;}
    const auto &reply=*source.reply;
    if(!aligned(anchor_,reply.began)||!aligned(anchor_,reply.finished)||!ordered(r.issued,reply.began)||!ordered(reply.began,reply.finished)||!ordered(reply.finished,now)||reply.status==R::Clock){
        halt(S::ClockFault);return view_;}
    if(fresh)previousFinished_=reply.finished;
    if(reply.status==R::Identity||reply.status==R::DeviceFault||reply.status==R::Invalid){halt(S::Faulted);return view_;}
    if(reply.status!=R::Ok){hold(reply.status==R::Unsupported?S::Unsupported:S::Unavailable);return view_;}
    view_.observed=reply.began;
    const auto age=(std::max)(static_cast<uint64_t>(now.utcMs-reply.began.utcMs),now.monotonicMs-reply.began.monotonicMs);
    if(age>=policy_.maximumAgeMs){hold(S::Unavailable);return view_;}
    if(now.monotonicMs>UINT64_MAX-(policy_.maximumAgeMs-age)){halt(S::ClockFault);return view_;}
    const auto calculated=now.monotonicMs+policy_.maximumAgeMs-age;
    sourceDeadline_=sourceDeadline_?(std::min)(sourceDeadline_,calculated):calculated;
    if(sourceDeadline_<=now.monotonicMs){hold(S::Unavailable);return view_;}
    const auto until=sourceDeadline_;
    if(fresh&&previousGood_&&(!ordered(previousGood_->finished,reply.began)||reply.began.monotonicMs==previousGood_->began.monotonicMs)){
        halt(S::ClockFault);return view_;}
    if(capabilities_.empty())for(const auto &reading:reply.readings){capabilities_.push_back(reading.supportedClockEvents);fieldCapabilities_.push_back(reading.fields);}
    bool hot=false,cool=true,unsupported=false;
    for(size_t i=0;i<reply.readings.size();++i){const auto &reading=reply.readings[i];
        if(reading.supportedClockEvents!=capabilities_[i]){halt(S::Faulted);return view_;}
        unsupported=unsupported||(reading.supportedClockEvents&coreThermalHoldEvents)!=coreThermalHoldEvents||(reading.activeClockEvents&~coreThermalKnownEvents);
        hot=hot||reading.temperatureC>=policy_.devices[i].pauseAtC||(reading.activeClockEvents&coreThermalHoldEvents);
        cool=cool&&reading.temperatureC<=policy_.devices[i].resumeAtOrBelowC;
        if(policy_.profile==SafetyTelemetryProfile::ExtendedFields){
            for(size_t j=0;j<reading.fields.size();++j){const auto &before=fieldCapabilities_[i][j],&after=reading.fields[j];
                if(before.valueType!=after.valueType||(j>=1&&j<=4&&before.value!=after.value)){halt(S::Faulted);return view_;}}
            const auto memory=reading.fields[0].value;
            hot=hot||memory>=policy_.devices[i].memoryPauseAtC;cool=cool&&memory<=policy_.devices[i].memoryResumeAtOrBelowC;
        }
    }
    if(unsupported){hold(S::Unsupported);return view_;}
    if(hot){hold(S::Cooling);previousGood_=reply;return view_;}
    if(fresh&&previousGood_&&(!contiguous||reply.began.monotonicMs-previousGood_->began.monotonicMs>policy_.maximumGapMs||
        static_cast<uint64_t>(reply.began.utcMs-previousGood_->began.utcMs)>policy_.maximumGapMs))hold(S::Cooling);
    const auto passing=policy_.profile==SafetyTelemetryProfile::ExtendedFields?S::ExtendedWithinLimits:S::CoreWithinLimits;
    if(admitted_){view_.state=passing;view_.validUntilMonoMs=until;previousGood_=reply;return view_;}
    if(!cool){hold(S::Cooling);previousGood_=reply;return view_;}
    if(fresh){
        if(!view_.coolingSince){view_.coolingSince=reply.finished;view_.coolSamples=1;}
        else if(view_.coolSamples<policy_.minimumCoolSamples)++view_.coolSamples;
        if(view_.coolSamples>=policy_.minimumCoolSamples&&ordered(*view_.coolingSince,reply.began)&&
            reply.began.monotonicMs-view_.coolingSince->monotonicMs>=policy_.coolingMs&&
            static_cast<uint64_t>(reply.began.utcMs-view_.coolingSince->utcMs)>=policy_.coolingMs)admitted_=true;
    }
    view_.state=admitted_?passing:S::Cooling;view_.validUntilMonoMs=admitted_?until:0;previousGood_=reply;return view_;
}
} // namespace om
