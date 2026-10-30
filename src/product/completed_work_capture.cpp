#include "completed_work_capture.h"
namespace om {
namespace {
using S=CompletedWorkState;using R=CompletedWorkStatus;
bool live(S s){return s==S::Ready||s==S::Running;}
ExactDecimal integer(uint64_t n){ExactDecimal d;ExactDecimal::parse(std::to_string(n),&d);return d;}
}
CompletedWorkCapture::CompletedWorkCapture(CompletedWorkPolicy p,const SessionIntent &i,SessionToken t,uint64_t epoch)
    :policy_(p),intent_(i),token_(t),counterEpoch_(epoch){}
bool CompletedWorkCapture::fail(R r)noexcept{attempted_=true;
    if(view_.state!=S::Stopped&&view_.state!=S::Faulted){view_.state=r==R::Stopped?S::Stopped:S::Faulted;view_.status=r;}return false;}
bool CompletedWorkCapture::time(CompletedWorkTime t)noexcept{
    if(!live(view_.state))return false;
    if(!intent_.permits(token_))return fail(R::Stopped);
    if(t.utcMs<view_.observed.utcMs||t.utcMs>253402300799999LL||t.monotonicMs<view_.observed.monotonicMs)return fail(R::Clock);
    const auto wall=static_cast<uint64_t>(t.utcMs-anchor_.utcMs),mono=t.monotonicMs-anchor_.monotonicMs;
    if((wall>mono?wall-mono:mono-wall)>2000)return fail(R::Clock);
    view_.observed=t;return true;
}
bool CompletedWorkCapture::start(CompletedWorkTime t)noexcept{
    if(attempted_)return false;
    attempted_=true;
    if(!intent_.permits(token_))return fail(R::Stopped);
    if(!policy_.enabled){view_.state=S::Disabled;view_.status=R::Disabled;return false;}
    if(!counterEpoch_||!policy_.maximumRuns||policy_.maximumRuns>8192)return fail(R::Invalid);
    if(t.utcMs<0||t.utcMs>253402300799999LL||!t.monotonicMs)return fail(R::Clock);
    anchor_=t;view_.observed=t;view_.generation=token_.epoch;view_.counterEpoch=counterEpoch_;
    view_.state=S::Ready;view_.status=R::Ok;return true;
}
bool CompletedWorkCapture::begin(WorkLane lane,CompletedWorkTime t)noexcept{
    if(!time(t))return false;
    if(view_.state!=S::Ready||(lane!=WorkLane::User&&lane!=WorkLane::Donation))return fail(R::Attribution);
    if(view_.runs>=policy_.maximumRuns)return fail(R::Capacity);
    ++view_.runs;view_.lane=lane;view_.state=S::Running;return true;
}
bool CompletedWorkCapture::completed(const CompletedWorkBatch &b)noexcept{
    try{if(!time(b.observed))return false;
        if(view_.state!=S::Running||!b.workUnits||!b.attempts||!b.macsPerUnit||b.workUnits%b.attempts||b.appliedHostWaitMs>1000||b.hostVerificationFailed)
            return fail(R::Invalid);
        const auto units=b.workUnits/b.attempts;
        if(view_.batches&&(view_.workUnitsPerAttempt!=units||view_.macsPerUnit!=b.macsPerUnit))return fail(R::Discontinuity);
        if(view_.batches==UINT64_MAX||b.attempts>UINT64_MAX-view_.attempts)return fail(R::Capacity);
        ExactDecimal factor,macs,delta,total;ExactDecimal::parse("0.000000000001",&factor);
        const auto &prior=view_.lane==WorkLane::User?view_.userTmacs:view_.donationTmacs;
        if(!integer(b.workUnits).multiply(integer(b.macsPerUnit),&macs)||!macs.multiply(factor,&delta)||!prior.add(delta,&total)||
            total.digits()>38||total.scale()>12)return fail(R::Capacity);
        if(view_.lane==WorkLane::User)view_.userTmacs=std::move(total);else view_.donationTmacs=std::move(total);
        ++view_.batches;view_.attempts+=b.attempts;view_.workUnitsPerAttempt=units;view_.macsPerUnit=b.macsPerUnit;
        view_.lastBatch=CompletedWorkOriginal{view_.lane,view_.runs,view_.batches,b};return true;
    }catch(...){return fail(R::Capacity);}
}
bool CompletedWorkCapture::end(bool reconciled,CompletedWorkTime t)noexcept{
    if(!time(t))return false;
    if(view_.state!=S::Running)return fail(R::Attribution);
    if(!reconciled)return fail(R::Discontinuity);
    view_.state=S::Ready;return true;
}
void CompletedWorkCapture::requestStop()noexcept{fail(R::Stopped);}
} // namespace om
