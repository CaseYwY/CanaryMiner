#include "billing_compute_owner.h"
#include "billing_model.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace om {
namespace {
using O=BillingComputeState;using A=BillingComputeAdmission;using S=BillingStatus;using E=EnergyHistoryStatus;
bool hash(const std::string &s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
BillingOwnerTime systemTime(){return {std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(),
    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())};}
bool settled(const BillingOwnerSnapshot &s){
    if(s.state!=BillingOwnerState::Ready||s.pending||!s.reviewable||s.uncertain||!s.context||!s.view||!s.view->stable||!s.view->stage||!s.view->monetary)return false;
    const auto &stage=*s.view->stage;const auto &m=*s.view->monetary;
    return stage.status==S::Ok&&!stage.replacementPending&&(!stage.present||stage.phase==BillingInputPhase::Complete)&&m.status==S::Ok&&m.committed.status==S::Ok&&
        (m.state==BillingRecoveryState::Empty||m.state==BillingRecoveryState::Confirmed||m.state==BillingRecoveryState::Rejected)&&m.committed.source&&!m.committed.settledTotal;
}
bool same(const BillingSourceNotice &a,const BillingSourceNotice &b){return a.checkpoint.sequence==b.checkpoint.sequence&&a.checkpoint.digest==b.checkpoint.digest&&a.throughUtcMs==b.throughUtcMs&&a.observedUtcMs==b.observedUtcMs;}
bool older(const EnergyHistoryAnchor &a,const EnergyHistoryAnchor &b){return a.sequence<b.sequence||(a.sequence==b.sequence&&a.digest!=b.digest);}
}
struct BillingComputeOwner::Impl {
    struct Job {uint64_t ticket=0,epoch=0;int64_t now=0;BillingSourceNotice notice;std::shared_ptr<const BillingOwnerSnapshot> review;std::atomic<bool> cancel{false};};
    struct Result {LocalTaskStatus transport=LocalTaskStatus::Unavailable;S status=S::Storage;std::optional<E> sourceStatus;std::optional<ElectricityStatus> calculation;
        std::optional<EnergyHistoryCoverage> coverage;
        std::optional<EnergyHistoryAnchor> source;std::shared_ptr<const BillingCandidate> candidate;};
    std::filesystem::path helper;std::shared_ptr<const BillingOwnerBinding> context;BillingComputePolicy policy;BillingOwnerClock clock;
    EnergyTraceProcessClient client;mutable std::mutex mutex;std::mutex lifecycle;std::condition_variable wake;std::thread thread;
    bool attempted=false,stopped=true;uint64_t epoch=0,lastMonetaryRevision=0;BillingOwnerTime base,last;
    std::shared_ptr<Job> queued,flight;std::optional<BillingSourceNotice> lastNotice;uint64_t lastNoticeRevision=0;
    std::optional<EnergyHistoryAnchor> sourceWater,moneyWater;
    BillingComputeSnapshot current;std::shared_ptr<const BillingComputeSnapshot> published=std::make_shared<const BillingComputeSnapshot>();
    const std::shared_ptr<const BillingComputeSnapshot> emptyTerminal=published;
#ifdef CANARY_BILLING_COMPUTE_TESTING
    std::atomic<bool> unreapedNext{false};
#endif
    Impl(std::filesystem::path h,std::shared_ptr<const BillingOwnerBinding> b,BillingComputePolicy p,BillingOwnerClock c)
        :helper(std::move(h)),context(std::move(b)),policy(p),clock(c?std::move(c):BillingOwnerClock(systemTime)),client(helper){}
    void publish(){current.pending=!stopped&&static_cast<bool>(queued||flight);++current.revision;published=std::make_shared<const BillingComputeSnapshot>(current);}
    void cancel(){if(queued)queued->cancel=true;if(flight)flight->cancel=true;queued.reset();current.reviewable=false;}
    void halt(O state,S status){stopped=true;cancel();current.state=state;current.status=status;publish();wake.notify_all();}
    bool time(){const auto now=clock();
        if(!billing_detail::instant(now.utcMs)||now.utcMs<last.utcMs||now.monotonicMs<last.monotonicMs||now.monotonicMs-base.monotonicMs>static_cast<uint64_t>(billing_detail::lastUtc)){
            halt(O::ClockFault,S::Clock);return false;}
        const auto elapsed=static_cast<int64_t>(now.monotonicMs-base.monotonicMs),wall=now.utcMs-base.utcMs;
        if(elapsed>wall+2000||wall>elapsed+2000){halt(O::ClockFault,S::Clock);return false;}last=now;return true;
    }
    A check(uint64_t g,uint64_t r){
        if(stopped)return A::Stopped;
        if(g!=current.generation)return A::StaleGeneration;
        if(!r||r!=current.revision)return A::ReviewChanged;
        if(queued||flight||!current.monetary||!settled(*current.monetary))return A::NotReady;
        return time()?A::Queued:A::Stopped;
    }
    A enqueue(const BillingSourceNotice &notice,bool explicitRefresh){
        const auto &m=current.monetary->view->monetary->committed;
        if(!notice.checkpoint.sequence||!hash(notice.checkpoint.digest)||!billing_detail::instant(notice.observedUtcMs)||notice.observedUtcMs>last.utcMs||
            notice.throughUtcMs<=m.opening.throughUtcMs||notice.throughUtcMs>notice.observedUtcMs||older(notice.checkpoint,m.source->checkpoint))return A::Invalid;
        if(!explicitRefresh&&lastNotice&&lastNoticeRevision==current.monetary->revision&&same(*lastNotice,notice))return A::Duplicate;
        if(current.lastTicket==UINT64_MAX||m.sequence>=context->limits.maxBatches||m.intervals>=context->limits.maxIntervals)return A::NotReady;
        auto j=std::make_shared<Job>();j->ticket=current.lastTicket+1;j->epoch=epoch;j->now=last.utcMs;j->review=current.monetary;j->notice=notice;
        auto next=current;next.state=O::Busy;next.reviewable=false;next.pending=true;next.lastTicket=j->ticket;++next.revision;
        auto view=std::make_shared<const BillingComputeSnapshot>(next);lastNotice=notice;lastNoticeRevision=j->review->revision;queued=std::move(j);current=std::move(next);published=std::move(view);wake.notify_one();return A::Queued;
    }
    Result compute(const std::shared_ptr<Job> &j){
        Result out;const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(policy.operationTimeoutMs);
        const auto &b=context->binding;const auto &m=j->review->view->monetary->committed;
        // Fixed clocks keep candidate arithmetic in-memory; named-data runtime/loader qualification remains separate.
        if(b.plan.clock.kind==CivilClockKind::NamedIana){out.transport=LocalTaskStatus::Invalid;out.status=S::Unsupported;return out;}
        out.status=billing_detail::validateBinding(b,context->limits,j->now);
        if(out.status!=S::Ok||!b.source||!m.source){out.transport=LocalTaskStatus::Invalid;return out;}
        if(j->now<m.observedUtcMs||j->now<m.source->queriedUtcMs){out.status=S::Clock;out.transport=LocalTaskStatus::Invalid;return out;}
        const auto end=(std::min)(j->notice.throughUtcMs,b.cycleEndUtcMs);
        if(end<=m.opening.throughUtcMs){out.status=S::Invalid;out.transport=LocalTaskStatus::Invalid;return out;}
        EnergyTraceRequest q;const auto &source=*b.source;q.source.generation=current.generation;q.source.ticket=j->ticket;
        q.source.file=source.file;q.source.binding=source.binding;q.source.limits=source.limits;q.source.recovery=EnergyRecoveryMode::Inspect;q.source.nowUtcMs=j->now;
        q.source.queries={{m.opening.throughUtcMs,end,"",""}};q.anchor=m.source->checkpoint;q.maxIntervals=policy.maxIntervals;
        unsigned remaining=0;
        {std::lock_guard<std::mutex> lock(mutex);if(stopped||j->cancel||j->epoch!=epoch||!time()){out.transport=LocalTaskStatus::Cancelled;return out;}
            const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
            if(left<50){out.transport=LocalTaskStatus::Timeout;return out;}remaining=static_cast<unsigned>(left);}
        out.status=S::Storage;const auto read=client.transact(q,j->cancel,remaining);out.transport=read.transport;
        if(!read.reply)return out;
        out.sourceStatus=read.reply->status;const auto &checkpoint=read.reply->source.checkpoint;
        if(!read.reply->source.totals.empty())out.coverage=read.reply->source.totals[0].coverage;
        if(read.reply->source.opened==E::Ok&&checkpoint.status==E::Ok)out.source=EnergyHistoryAnchor{checkpoint.sequence,checkpoint.digest};
        auto result=std::make_shared<BillingCandidate>();out.status=makeBillingSourceChunk(m.sequence+1,q,*read.reply,&result->original);
        if(out.status!=S::Ok)return out;
        if(result->original.observedUtcMs<m.observedUtcMs){out.status=S::Clock;return out;}
        if(result->original.trace.intervals.size()>context->limits.maxIntervals-m.intervals){out.status=S::Capacity;return out;}
        result->preview=accrueBill(b.plan,m.opening,result->original.trace,end,result->original.observedUtcMs);out.calculation=result->preview.status;
        if(result->preview.status!=ElectricityStatus::Ok){out.status=S::Calculation;return out;}
        if(std::chrono::steady_clock::now()>=deadline){out.transport=LocalTaskStatus::Timeout;out.status=S::Storage;return out;}
        result->reviewed=j->review;result->notice=j->notice;result->queriedUtcMs=j->now;out.candidate=std::move(result);return out;
    }
    void run()noexcept{try{for(;;){std::shared_ptr<Job> j;
        {std::unique_lock<std::mutex> lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(25),[&]{return stopped||queued;});if(stopped||!time())return;
            if(!queued)continue;
            flight=queued;queued.reset();j=flight;}
        auto out=compute(j);
#ifdef CANARY_BILLING_COMPUTE_TESTING
        if(unreapedNext.exchange(false))out.transport=LocalTaskStatus::Unreaped;
#endif
        std::lock_guard<std::mutex> lock(mutex);
        if(stopped){flight.reset();return;}
        if(out.transport==LocalTaskStatus::Unreaped){current.transport=out.transport;halt(O::Faulted,S::Storage);flight.reset();return;}
        if(j->cancel||j->epoch!=epoch){flight.reset();publish();continue;}
        if(!time()){flight.reset();return;}
        if(out.source){if(sourceWater&&older(*out.source,*sourceWater)){halt(O::Faulted,S::Attribution);flight.reset();return;}sourceWater=out.source;}
        current.transport=out.transport;current.status=out.status;current.sourceStatus=out.sourceStatus;current.calculation=out.calculation;current.completedTicket=j->ticket;
        current.sourceCoverage=out.coverage;
        current.reviewable=static_cast<bool>(out.candidate);if(out.candidate)current.candidate=std::move(out.candidate);
        current.state=current.reviewable?O::Ready:O::Unavailable;flight.reset();publish();
    }}catch(...){try{std::lock_guard<std::mutex> lock(mutex);if(!stopped)halt(O::Faulted,S::Storage);flight.reset();}catch(...){} }}
};
BillingComputeOwner::BillingComputeOwner(std::filesystem::path h,std::shared_ptr<const BillingOwnerBinding> b,BillingComputePolicy p,BillingOwnerClock c):impl_(new Impl(std::move(h),std::move(b),p,std::move(c))){}
BillingComputeOwner::~BillingComputeOwner(){stop();}
bool BillingComputeOwner::start(uint64_t generation){auto &s=*impl_;std::lock_guard<std::mutex> life(s.lifecycle);std::lock_guard<std::mutex> lock(s.mutex);
    if(s.attempted)return false;
    s.attempted=true;
    if(!generation||!s.policy.enabled||s.policy.operationTimeoutMs<500||s.policy.operationTimeoutMs>60000||!s.policy.maxIntervals||s.policy.maxIntervals>512||!localTaskPath(s.helper)||!s.context||!s.context->binding.source)return false;
    try{s.base=s.last=s.clock();if(!billing_detail::instant(s.base.utcMs))return false;s.current.generation=generation;s.current.state=O::Waiting;s.stopped=false;s.publish();s.thread=std::thread([p=&s]{p->run();});return true;}
    catch(...){s.stopped=true;s.current.state=O::Faulted;try{s.publish();}catch(...){}return false;}
}
bool BillingComputeOwner::observe(uint64_t generation,std::shared_ptr<const BillingOwnerSnapshot> monetary){
    auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);
    if(s.stopped||generation!=s.current.generation||!monetary||monetary->generation!=generation||!monetary->revision||monetary->context!=s.context||s.epoch==UINT64_MAX)return false;
    try{if(!s.time())return false;
        if(monetary==s.current.monetary)return true;
        if(monetary->revision<=s.lastMonetaryRevision)return false;
        s.lastMonetaryRevision=monetary->revision;
        ++s.epoch;s.cancel();s.current.monetary=std::move(monetary); // retain the bounded hint only for a new explicit recompute
        if(s.current.monetary->state==BillingOwnerState::Stopped){s.halt(O::Stopped,s.current.status);return true;}
        if(s.current.monetary->state==BillingOwnerState::ClockFault){s.halt(O::ClockFault,S::Clock);return true;}
        if(s.current.monetary->state==BillingOwnerState::Faulted){s.halt(O::Faulted,S::Attribution);return true;}
        if(s.current.monetary->reviewable&&s.current.monetary->view&&s.current.monetary->view->monetary&&s.current.monetary->view->monetary->status==S::Ok){
            const auto &m=*s.current.monetary->view->monetary;const EnergyHistoryAnchor head{m.committed.sequence,m.committed.digest};
            if(m.state==BillingRecoveryState::Diverged||(s.moneyWater&&older(head,*s.moneyWater))){s.halt(O::Faulted,S::Attribution);return true;}s.moneyWater=head;}
        s.current.state=O::Waiting;s.publish();return true;
    }catch(...){return false;}
}
BillingComputeAdmission BillingComputeOwner::notify(uint64_t g,uint64_t r,const BillingSourceNotice &notice){auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);
    try{const auto checked=s.check(g,r);return checked==A::Queued?s.enqueue(notice,false):checked;}catch(...){return A::Invalid;}}
BillingComputeAdmission BillingComputeOwner::refresh(uint64_t g,uint64_t r){auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);
    try{const auto checked=s.check(g,r);return checked==A::Queued?(s.lastNotice?s.enqueue(*s.lastNotice,true):A::NotReady):checked;}catch(...){return A::Invalid;}}
bool BillingComputeOwner::claimReviewed(uint64_t g,uint64_t r,uint64_t mg,uint64_t mr){auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);
    try{if(s.check(g,r)!=A::Queued||!s.current.reviewable||!s.current.candidate||s.current.candidate->reviewed!=s.current.monetary||
            mg!=s.current.monetary->generation||mr!=s.current.monetary->revision)return false;
        auto next=s.current;next.reviewable=false;next.state=O::Waiting;++next.revision;
        auto published=std::make_shared<const BillingComputeSnapshot>(next);s.current=std::move(next);s.published=std::move(published);return true;
    }catch(...){return false;}
}
bool BillingComputeOwner::invalidate(uint64_t g){auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);if(s.stopped||g!=s.current.generation||s.epoch==UINT64_MAX)return false;
    ++s.epoch;s.cancel();s.current.monetary.reset();s.lastNotice.reset();s.current.state=O::Waiting;try{s.publish();}catch(...){}s.wake.notify_all();return true;}
void BillingComputeOwner::requestStop(){auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);s.attempted=true;if(s.current.state==O::Stopped)return;
    try{s.halt(O::Stopped,s.current.status);}catch(...){s.stopped=true;s.cancel();s.current.state=O::Stopped;s.current.reviewable=false;}s.wake.notify_all();}
void BillingComputeOwner::stop(){auto &s=*impl_;std::lock_guard<std::mutex> life(s.lifecycle);requestStop();if(s.thread.joinable())s.thread.join();std::lock_guard<std::mutex> lock(s.mutex);s.flight.reset();}
std::shared_ptr<const BillingComputeSnapshot> BillingComputeOwner::snapshot()const{std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->published->revision!=impl_->current.revision||impl_->published->state!=impl_->current.state||impl_->published->reviewable!=impl_->current.reviewable?impl_->emptyTerminal:impl_->published;}
#ifdef CANARY_BILLING_COMPUTE_TESTING
void BillingComputeTestPeer::unreapedNext(BillingComputeOwner &owner){owner.impl_->unreapedNext=true;}
#endif
}
