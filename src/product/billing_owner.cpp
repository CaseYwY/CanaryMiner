#include "billing_owner.h"
#include "billing_input_model.h"
#include "billing_retry_process.h"
#include "hash.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace om {
namespace {
using S=BillingStatus;using R=BillingRecoveryState;using P=BillingInputPhase;
using A=BillingOwnerAction;using D=BillingOwnerAdmission;using O=BillingOwnerState;
using namespace billing_detail;
BillingOwnerTime systemTime(){return {std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(),
    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())};}
bool settled(const BillingRecoverySnapshot &s){return s.status==S::Ok&&s.committed.status==S::Ok&&(s.state==R::Empty||s.state==R::Confirmed||s.state==R::Rejected);}
bool sameStage(const BillingInputSnapshot &a,const BillingInputSnapshot &b){return a.status==S::Ok&&b.status==S::Ok&&a.present==b.present&&a.replacementPending==b.replacementPending&&a.revision==b.revision;}
bool sameView(const BillingOwnerView &a,const BillingOwnerView &b){return a.stable&&b.stable&&a.stage&&b.stage&&sameStage(*a.stage,*b.stage)&&a.monetary&&b.monetary&&
    a.monetary->status==b.monetary->status&&a.monetary->state==b.monetary->state&&a.monetary->revision==b.monetary->revision&&
    a.monetary->committed.sequence==b.monetary->committed.sequence&&a.monetary->committed.digest==b.monetary->committed.digest;}
bool sameManifest(const BillingInputManifest &a,const BillingInputManifest &b){return billing_input_detail::manifestText(a)==billing_input_detail::manifestText(b);}
bool older(const BillingSnapshot &a,const BillingSnapshot &b){return a.sequence<b.sequence||(a.sequence==b.sequence&&a.digest!=b.digest);}
bool sameOriginal(const BillingChunk &a,const BillingChunk &b){return chunkText(a)==chunkText(b);}
bool boundedOriginal(const BillingChunk &c){
    if(!boundedChunk(c))return false;
    if(!c.receipt)return true;
    const auto &r=c.receipt->request;const auto &reply=c.receipt->reply;const auto &s=reply.source;const auto &h=s.checkpoint;
    // Reply encoders validate after serialization. Bound every borrowed variable-size field BEFORE invoking them.
    if(r.source.file.native().size()>4096||!validEnergyTraceRequest(r)||s.totals.size()>4||h.batchId.size()>128||h.sessionId.size()>128||h.digest.size()>64||
        !s.recovery||s.recovery->priorDigest.size()>64||!reply.completeTrace||reply.completeTrace->intervals.size()>512||
        reply.completeTrace->evidence.scope.size()>128||reply.completeTrace->evidence.adapter.size()>128)return false;
    return !s.recovery->batch||boundedEnergyHistoryBatch(r.source.binding,*s.recovery->batch,r.source.nowUtcMs);
}
}
struct BillingOwner::Impl {
    struct Job {
        A action=A::Refresh;uint64_t ticket=0,epoch=0;int64_t now=0;
        std::atomic<bool> cancel{false};
        std::shared_ptr<const BillingOwnerView> review;
        std::shared_ptr<const BillingChunk> original;
        std::shared_ptr<const BillingOwnerIntent> issued; // set under mutex immediately before helper handoff
    };
    struct Result {
        LocalTaskStatus transport=LocalTaskStatus::Unavailable;S status=S::Storage;
        std::shared_ptr<const BillingOwnerView> view;
        std::shared_ptr<const BillingRecoverySnapshot> proof;
        bool resolved=false;
    };
    BillingOwnerHelpers helpers;
    std::shared_ptr<const BillingOwnerBinding> context;
    BillingOwnerPolicy policy;BillingOwnerClock clock;
    BillingInputQueryClient stageClient;BillingQueryProcessClient moneyClient;BillingInputProcessClient inputClient;BillingRetryProcessClient retryClient;
    mutable std::mutex mutex;std::mutex lifecycle;std::condition_variable wake;std::thread thread;
    bool attempted=false,stopped=true,admitting=false;uint64_t epoch=0;
#ifdef CANARY_BILLING_OWNER_TESTING
    std::atomic<bool> unreapedNext{false};
#endif
    BillingOwnerTime base,last;std::optional<BillingSnapshot> highWater;
    std::shared_ptr<Job> queued,flight;
    BillingOwnerSnapshot current;
    std::shared_ptr<const BillingOwnerSnapshot> published=std::make_shared<const BillingOwnerSnapshot>();
    const std::shared_ptr<const BillingOwnerSnapshot> emptyTerminal=published;
    Impl(BillingOwnerHelpers h,BillingOwnerBinding b,BillingOwnerPolicy p,BillingOwnerClock c)
        :helpers(std::move(h)),context(std::make_shared<const BillingOwnerBinding>(std::move(b))),policy(p),clock(c?std::move(c):BillingOwnerClock(systemTime)),
         stageClient(helpers.stageQuery),moneyClient(helpers.monetaryQuery),inputClient(helpers.input),retryClient(helpers.retry){}
    void publish(){current.pending=!stopped&&static_cast<bool>(queued||flight);++current.revision;published=std::make_shared<const BillingOwnerSnapshot>(current);}
    void retain(){if(flight&&flight->issued&&!current.uncertain)current.uncertain=flight->issued;}
    void cancelJobs(){retain();if(queued)queued->cancel=true;if(flight)flight->cancel=true;queued.reset();}
    void halt(O state,S status){stopped=true;cancelJobs();current.state=state;current.status=status;current.reviewable=false;publish();wake.notify_all();}
    bool time(){const auto now=clock();
        if(!instant(now.utcMs)||now.utcMs<last.utcMs||now.monotonicMs<last.monotonicMs||now.monotonicMs-base.monotonicMs>static_cast<uint64_t>(lastUtc)){
            halt(O::ClockFault,S::Clock);return false;}
        const auto elapsed=static_cast<int64_t>(now.monotonicMs-base.monotonicMs),wall=now.utcMs-base.utcMs;
        if(elapsed>wall+2000||wall>elapsed+2000){halt(O::ClockFault,S::Clock);return false;}
        last=now;return true;
    }
    D check(uint64_t generation,uint64_t revision){
        if(stopped)return D::Stopped;
        if(generation!=current.generation)return D::StaleGeneration;
        if(!revision||revision!=current.revision)return D::ReviewChanged;
        if(admitting||queued||flight)return D::NotReady;
        return time()?D::Queued:D::Stopped;
    }
    bool eligible(A action)const{
        if(action==A::Refresh)return true;
        const auto &v=current.view;
        if(!current.reviewable||!v||!v->stable||!v->stage||!v->monetary||v->stage->replacementPending)return false;
        const auto &s=*v->stage;const auto &m=*v->monetary;
        const bool free=!s.present||s.phase==P::Complete;
        switch(action){
            case A::CreateNew:return !current.uncertain&&!highWater&&!s.present&&m.status==S::Storage;
            case A::Submit:return !current.uncertain&&free&&settled(m);
            case A::Commit:
                if(!s.present||s.phase!=P::Receiving||s.received!=s.manifest.bindingBytes+s.manifest.chunkBytes)return false;
                if(current.uncertain&&(!current.uncertain->manifest||!sameManifest(*current.uncertain->manifest,s.manifest)))return false;
                return s.manifest.mode==BillingInputMode::CreateNew?!highWater&&m.status==S::Storage:
                    settled(m)&&m.revision==s.manifest.priorRevision&&m.committed.sequence==s.manifest.priorSequence&&m.committed.digest==s.manifest.priorDigest;
            case A::Retry:return m.status==S::Ok&&m.original&&(m.state==R::Prepared||m.state==R::Confirmed)&&
                (!current.uncertain||(current.uncertain->original&&sameOriginal(*current.uncertain->original,*m.original)));
            case A::Reconcile:return s.present&&s.phase==P::Issued&&s.manifest.mode==BillingInputMode::Submit&&m.status==S::Ok&&
                (!current.uncertain||(current.uncertain->manifest&&sameManifest(*current.uncertain->manifest,s.manifest)));
            default:return false;
        }
    }
    D enqueue(A action,std::shared_ptr<const BillingChunk> original={}){
        if(!eligible(action)||current.lastTicket>=UINT64_MAX/4-1)return D::NotReady;
        auto job=std::make_shared<Job>();job->action=action;job->ticket=current.lastTicket+1;job->epoch=epoch;job->now=last.utcMs;job->review=current.view;job->original=std::move(original);
        auto next=current;next.lastTicket=job->ticket;next.state=O::Busy;next.pending=true;next.reviewable=false;next.action=action;next.actionProof.reset();++next.revision;
        auto view=std::make_shared<const BillingOwnerSnapshot>(next);queued=std::move(job);current=std::move(next);published=std::move(view);wake.notify_one();return D::Queued;
    }
    D admit(uint64_t generation,uint64_t revision,A action){
        std::lock_guard<std::mutex> lock(mutex);
        try{const auto c=check(generation,revision);return c==D::Queued?enqueue(action):c;}catch(...){return D::Invalid;}
    }
    unsigned budget(const std::shared_ptr<Job> &job,std::chrono::steady_clock::time_point deadline,Result &r){
        std::lock_guard<std::mutex> lock(mutex);
        if(stopped||job->cancel||job->epoch!=epoch||!time()){r.transport=LocalTaskStatus::Cancelled;return 0;}
        const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
        if(left<50){r.transport=LocalTaskStatus::Timeout;return 0;}return static_cast<unsigned>(left);
    }
    BillingQueryRequest request(const Job &j,unsigned part=0)const{return {current.generation,j.ticket*4+part,context->file,context->binding,context->limits,j.now};}
    Result read(const std::shared_ptr<Job> &job,std::chrono::steady_clock::time_point deadline){
#ifdef CANARY_BILLING_OWNER_TESTING
        if(unreapedNext.exchange(false)){Result failed;failed.transport=LocalTaskStatus::Unreaped;return failed;}
#endif
        Result r;auto q=request(*job);auto ms=budget(job,deadline,r);if(!ms)return r;
        auto first=stageClient.transact(q,job->cancel,ms);r.transport=first.transport;
        if(!first.reply||first.reply->status!=S::Ok){if(first.reply)r.status=first.reply->status;return r;}
        ms=budget(job,deadline,r);if(!ms)return r;q.ticket++;
        auto money=moneyClient.transact(q,job->cancel,ms);r.transport=money.transport;
        if(!money.reply)return r;
        ms=budget(job,deadline,r);if(!ms)return r;q.ticket++;
        auto second=stageClient.transact(q,job->cancel,ms);r.transport=second.transport;
        if(!second.reply||second.reply->status!=S::Ok){if(second.reply)r.status=second.reply->status;return r;}
        if(!sameStage(*first.reply,*second.reply)){r.status=S::Conflict;return r;}
        auto v=std::make_shared<BillingOwnerView>();v->observedUtcMs=job->now;v->stable=true;
        v->stage=std::make_shared<const BillingInputSnapshot>(std::move(*second.reply));v->monetary=std::make_shared<const BillingRecoverySnapshot>(std::move(*money.reply));
        r.status=v->monetary->status;r.view=std::move(v);return r;
    }
    bool observe(const BillingOwnerView &v){
        if(!v.monetary||v.monetary->status!=S::Ok)return true;
        const auto &m=*v.monetary;
        if(m.state==R::Diverged||(highWater&&older(m.committed,*highWater))||
            (v.stage&&v.stage->outcome&&older(m.committed,v.stage->outcome->committed))){halt(O::Faulted,S::Attribution);return false;}
        highWater=m.committed;return true;
    }
    Result execute(const std::shared_ptr<Job> &job){
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(policy.operationTimeoutMs);
        auto r=read(job,deadline);
        if(r.transport==LocalTaskStatus::Unreaped)return r;
        {std::lock_guard<std::mutex> lock(mutex);if(stopped||job->cancel||job->epoch!=epoch)return r;
            if(r.view&&!observe(*r.view))return r;}
        if(std::chrono::steady_clock::now()>=deadline){r.transport=LocalTaskStatus::Timeout;r.view.reset();return r;}
        if(job->action==A::Refresh||!r.view)return r;
        if(!job->review||!sameView(*job->review,*r.view)){r.status=S::Conflict;return r;}
        auto q=request(*job,3);auto intent=std::make_shared<BillingOwnerIntent>();intent->action=job->action;intent->original=job->original;intent->monetary=job->review->monetary;
        const auto &stage=*job->review->stage;BillingInputPlan plan;
        if(job->action==A::CreateNew||job->action==A::Submit){
            plan.context=q;if(job->original){plan.chunk=*job->original;plan.prior=*job->review->monetary;}
            BillingInputManifest manifest;std::string payload;
            if(!prepareBillingInput(plan,&manifest,&payload)){r.transport=LocalTaskStatus::Invalid;r.status=S::Invalid;return r;}
            intent->manifest=std::move(manifest);
        }else if(job->action==A::Commit||job->action==A::Reconcile){
            intent->manifest=stage.manifest;if(stage.original)intent->original=std::shared_ptr<const BillingChunk>(job->review->stage,&*stage.original);
        }else if(job->action==A::Retry){intent->original=std::shared_ptr<const BillingChunk>(intent->monetary,&*intent->monetary->original);}
        auto ms=budget(job,deadline,r);if(!ms)return r;
        {std::lock_guard<std::mutex> lock(mutex);if(stopped||job->cancel||job->epoch!=epoch)return r;job->issued=intent;}
        r.view.reset(); // a pre-write read is never published as a post-write current snapshot
        if(job->action==A::CreateNew||job->action==A::Submit){
            const auto result=inputClient.transact(plan,stage.present?stage.revision:"",job->cancel,ms);r.transport=result.transport;
            r.status=result.reply?result.reply->status:S::Storage;
            if(result.confirmed){r.proof=std::make_shared<const BillingRecoverySnapshot>(*result.confirmed);r.status=result.confirmed->applied.value_or(S::Ok);r.resolved=true;}
        }else if(job->action==A::Retry){
            const auto result=retryClient.transact(q,*job->review->monetary,job->cancel,ms);r.transport=result.transport;r.status=result.applied.value_or(S::Storage);
            if(result.confirmed){r.proof=std::make_shared<const BillingRecoverySnapshot>(*result.confirmed);r.resolved=true;}
        }else{
            BillingInputRequest request;request.generation=q.generation;request.ticket=q.ticket;request.manifest=stage.manifest;request.expectedRevision=stage.revision;
            request.action=job->action==A::Commit?BillingInputAction::Commit:BillingInputAction::Reconcile;
            const auto result=inputClient.exchange(request,job->cancel,ms);r.transport=result.transport;r.status=result.reply?result.reply->status:S::Storage;
            if(result.reply&&result.reply->status==S::Ok){const auto &s=*result.reply;
                const auto payload=bindingText(context->binding,context->limits)+stage.originalPrefix;BillingRecoverySnapshot proof;
                if(s.phase!=P::Complete||!s.certificate||s.received!=payload.size()||s.payloadDigest!=sha256Hex(payload)||
                    s.revision!=sha256Hex(billing_input_detail::stageBody(stage.manifest,s.phase,s.certificate,payload))||
                    !billing_input_detail::proof(stage.manifest,context->binding,context->limits,stage.original,*s.certificate,&proof)){
                    r.transport=LocalTaskStatus::Unavailable;r.status=S::Storage;
                }else{r.resolved=true;r.status=s.certificate->result;if(s.certificate->state!=R::Unavailable)r.proof=std::make_shared<const BillingRecoverySnapshot>(std::move(proof));}
            }
        }
        if(r.transport!=LocalTaskStatus::Unreaped&&!budget(job,deadline,r)){r.proof.reset();r.resolved=false;}
        return r;
    }
    bool resolvedByView(const BillingOwnerView &v)const{
        if(!current.uncertain||!v.stable||!v.stage||v.stage->replacementPending)return false;
        const auto &u=*current.uncertain;const auto &s=*v.stage;
        if(u.manifest)return s.present&&s.phase==P::Complete&&sameManifest(*u.manifest,s.manifest);
        return u.action==A::Retry&&u.original&&v.monetary&&v.monetary->status==S::Ok&&v.monetary->state==R::Confirmed&&v.monetary->original&&sameOriginal(*u.original,*v.monetary->original);
    }
    void run()noexcept{
        try{for(;;){std::shared_ptr<Job> job;
            {std::unique_lock<std::mutex> lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(25),[&]{return stopped||queued;});if(stopped||!time())return;
                if(!queued)continue;
                flight=queued;queued.reset();job=flight;}
            auto result=execute(job);
#ifdef CANARY_BILLING_OWNER_TESTING
            if(unreapedNext.exchange(false))result.transport=LocalTaskStatus::Unreaped;
#endif
            std::lock_guard<std::mutex> lock(mutex);
            if(stopped){flight.reset();return;}
            if(result.transport==LocalTaskStatus::Unreaped){current.transport=result.transport;halt(O::Faulted,S::Storage);flight.reset();return;}
            if(job->cancel||job->epoch!=epoch){flight.reset();publish();continue;}
            if(!time()){flight.reset();return;}
            current.transport=result.transport;current.status=result.status;current.completedTicket=job->ticket;current.actionProof=result.proof;
            if(result.proof){if(highWater&&older(result.proof->committed,*highWater)){halt(O::Faulted,S::Attribution);flight.reset();return;}highWater=result.proof->committed;}
            if(job->issued&&!result.resolved&&!current.uncertain)current.uncertain=job->issued;
            if(result.resolved){
                // An exact retry can confirm money while the older submission's Issued stage still needs Reconcile.
                if(!current.uncertain||job->action!=A::Retry||current.uncertain->action==A::Retry)current.uncertain.reset();
            }
            current.reviewable=static_cast<bool>(result.view);
            if(result.view){current.view=std::move(result.view);if(resolvedByView(*current.view))current.uncertain.reset();}
            const auto &v=current.view;
            current.state=current.reviewable&&!current.uncertain&&v&&v->stage&&!v->stage->replacementPending&&(!v->stage->present||v->stage->phase==P::Complete)&&
                v->monetary&&settled(*v->monetary)?O::Ready:O::NeedsReview;
            flight.reset();publish();
        }}catch(...){try{std::lock_guard<std::mutex> lock(mutex);if(!stopped){halt(O::Faulted,S::Storage);flight.reset();}}catch(...){} }
    }
};
BillingOwner::BillingOwner(BillingOwnerHelpers h,BillingOwnerBinding b,BillingOwnerPolicy p,BillingOwnerClock c):impl_(new Impl(std::move(h),std::move(b),p,std::move(c))){}
BillingOwner::~BillingOwner(){stop();}
bool BillingOwner::start(uint64_t generation){
    auto &s=*impl_;std::lock_guard<std::mutex> life(s.lifecycle);std::lock_guard<std::mutex> lock(s.mutex);if(s.attempted)return false;s.attempted=true;
    if(!generation||!s.policy.enabled||s.policy.operationTimeoutMs<500||s.policy.operationTimeoutMs>60000||!localTaskPath(s.context->file)||
        !localTaskPath(s.helpers.stageQuery)||!localTaskPath(s.helpers.monetaryQuery)||!localTaskPath(s.helpers.input)||!localTaskPath(s.helpers.retry))return false;
    try{s.base=s.last=s.clock();if(!instant(s.base.utcMs))return false;
        s.current.generation=generation;s.current.context=s.context;s.stopped=false;
        if(s.enqueue(A::Refresh)!=D::Queued){s.stopped=true;return false;}
        s.thread=std::thread([p=&s]{p->run();});return true;
    }catch(...){s.stopped=true;s.queued.reset();s.current.state=O::Faulted;try{s.publish();}catch(...){}return false;}
}
BillingOwnerAdmission BillingOwner::refresh(uint64_t g,uint64_t r){return impl_->admit(g,r,A::Refresh);}
BillingOwnerAdmission BillingOwner::createReviewed(uint64_t g,uint64_t r){return impl_->admit(g,r,A::CreateNew);}
BillingOwnerAdmission BillingOwner::commitReviewed(uint64_t g,uint64_t r){return impl_->admit(g,r,A::Commit);}
BillingOwnerAdmission BillingOwner::retryReviewed(uint64_t g,uint64_t r){return impl_->admit(g,r,A::Retry);}
BillingOwnerAdmission BillingOwner::reconcileReviewed(uint64_t g,uint64_t r){return impl_->admit(g,r,A::Reconcile);}
BillingOwnerAdmission BillingOwner::submitReviewed(uint64_t g,uint64_t r,const BillingChunk &chunk,const std::function<bool()> &claim){
    auto &s=*impl_;
    // Reserve the one admission slot before bounded copying, but never hold the stop mutex while serializing receipts.
    std::unique_lock<std::mutex> lock(s.mutex);
    try{
        const auto checked=s.check(g,r);if(checked!=D::Queued)return checked;
        if(!s.eligible(A::Submit))return D::NotReady;
        s.admitting=true;
        struct Release {Impl &s;std::unique_lock<std::mutex> &lock;~Release(){if(!lock.owns_lock())lock.lock();s.admitting=false;}} release{s,lock};
        // Only one borrowed-input admission may allocate; the final revision check rejects concurrent revocation.
        lock.unlock();if(!boundedOriginal(chunk))return D::Invalid;
        const auto text=chunkText(chunk);BillingChunk parsed;
        if(text.empty()||text.size()>chunkLimit||!parseChunk(text,s.context->binding.evidence,parsed))return D::Invalid;
        auto original=std::make_shared<const BillingChunk>(std::move(parsed));lock.lock();s.admitting=false;
        const auto final=s.check(g,r);if(final!=D::Queued)return final;
        if(!s.eligible(A::Submit))return D::NotReady;
        if(original->sequence!=s.current.view->monetary->committed.sequence+1||original->observedUtcMs>s.last.utcMs||
            (original->receipt&&original->receipt->request.source.nowUtcMs>s.last.utcMs))return D::Invalid;
        if(claim&&!claim())return D::ReviewChanged;
        return s.enqueue(A::Submit,std::move(original));
    }catch(...){return D::Invalid;}
}
bool BillingOwner::invalidate(uint64_t generation){
    auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);if(s.stopped||generation!=s.current.generation||s.epoch==UINT64_MAX)return false;
    ++s.epoch;s.cancelJobs();s.current.state=O::NeedsReview;s.current.reviewable=false;s.current.status=S::Conflict;
    try{s.publish();}catch(...){}s.wake.notify_all();return true;
}
void BillingOwner::requestStop(){
    auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);s.attempted=true;if(s.current.state==O::Stopped)return;
    try{s.halt(O::Stopped,s.current.status);}catch(...){s.stopped=true;s.cancelJobs();s.current.state=O::Stopped;s.current.reviewable=false;}s.wake.notify_all();
}
void BillingOwner::stop(){auto &s=*impl_;std::lock_guard<std::mutex> life(s.lifecycle);requestStop();if(s.thread.joinable())s.thread.join();std::lock_guard<std::mutex> lock(s.mutex);s.flight.reset();}
std::shared_ptr<const BillingOwnerSnapshot> BillingOwner::snapshot()const{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->published->revision!=impl_->current.revision||impl_->published->reviewable!=impl_->current.reviewable||impl_->published->state!=impl_->current.state?impl_->emptyTerminal:impl_->published;
}
#ifdef CANARY_BILLING_OWNER_TESTING
void BillingOwnerTestPeer::unreapedNext(BillingOwner &owner){owner.impl_->unreapedNext=true;}
#endif
} // namespace om
