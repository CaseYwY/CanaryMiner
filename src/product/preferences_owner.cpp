#include "preferences_owner.h"
#include "preferences_document.h"
#include "duplex_task_process.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace om {
struct PreferencesOwner::Impl {
    struct Job {PreferencesRequest request;std::string wire;std::chrono::steady_clock::time_point admitted;};
    std::filesystem::path helper;std::array<std::filesystem::path,8> targets;
    const SessionIntent &intent;SessionToken token;PreferencesOwnerPolicy policy;
    NativeProcessLifetime life;mutable std::mutex mutex;std::mutex lifecycle;std::condition_variable wake;
    std::atomic<bool> cancel{true};std::thread thread;bool attempted=false;
    std::chrono::steady_clock::time_point started;
    PreferencesOwnerSnapshot view,denied;
    std::shared_ptr<const Job> queued,inFlight;
    Impl(std::filesystem::path h,std::array<std::filesystem::path,8> t,const SessionIntent &i,SessionToken k,PreferencesOwnerPolicy p):helper(std::move(h)),targets(std::move(t)),intent(i),token(k),policy(p){denied.state=PreferencesOwnerState::Stopped;denied.transport=LocalTaskStatus::Cancelled;}
    bool foreign()const{return intent.foreignCaller(token)||(life.bound()&&life.current()&&!life.permitted());} // control thread only
    void retainWrite(){if(inFlight&&inFlight->request.operation==PreferencesOperation::Save&&!view.uncertain)view.uncertain=std::shared_ptr<const PreferencesRequest>(inFlight,&inFlight->request);}
    void revoke(PreferencesOwnerState state){ // mutex held; no allocation, file/process wait, clock or borrowed-intent mutation
        retainWrite();cancel=true;queued.reset();view.pending=view.inFlight=static_cast<bool>(inFlight);view.reviews.fill(0);view.inspectionReview=0;
        if(view.state!=PreferencesOwnerState::Stopped||state==PreferencesOwnerState::Stopped)view.state=state;
        wake.notify_all();
    }
    bool current(){ // control-thread callers only: SessionIntent is not read on the CPU/background thread
        if(life.bound()&&!life.current()){view.transport=LocalTaskStatus::Unreaped;revoke(PreferencesOwnerState::Faulted);return false;}
        if(!intent.permits(token)){revoke(PreferencesOwnerState::Stopped);return false;}
        return !cancel.load()&&view.state!=PreferencesOwnerState::Faulted&&view.state!=PreferencesOwnerState::Stopped;
    }
    bool nativeCurrent(){ // background thread: original native lifetime + atomic cancellation only
        if(life.current())return !cancel.load();
        view.transport=LocalTaskStatus::Unreaped;revoke(PreferencesOwnerState::Faulted);return false;
    }
    PreferencesSubmission enqueue(PreferencesOperation operation,PreferencesKind kind,uint64_t review,const std::string &document){
        PreferencesSubmission result;if(foreign()||!current())return result;
        const auto index=static_cast<unsigned>(kind);
        if(index>=8||targets[index].empty()){result.status=PreferencesAdmission::Invalid;return result;}
        if(view.state==PreferencesOwnerState::Starting||queued||inFlight||view.reply){result.status=PreferencesAdmission::Busy;return result;}
        if(view.uncertain&&(operation!=PreferencesOperation::Load||view.uncertain->kind!=kind)){result.status=PreferencesAdmission::NeedsInspection;return result;}
        if(operation==PreferencesOperation::Save&&(!review||view.reviews[index]!=review)){result.status=PreferencesAdmission::Stale;return result;}
        if(view.submitted>=UINT64_MAX-1){result.status=PreferencesAdmission::Invalid;return result;}
        try {
            const auto admitted=std::chrono::steady_clock::now();
            auto job=std::make_shared<Job>();job->admitted=admitted;job->request.ticket=view.submitted+1;job->request.kind=kind;job->request.operation=operation;job->request.review=review;job->request.document=document;
            if(!encodePreferencesRequest(job->request,&job->wire)){result.status=PreferencesAdmission::Invalid;return result;}
            queued=std::move(job);view.submitted=queued->request.ticket;view.kind=kind;view.operation=operation;view.pending=true;view.reviews[index]=0;view.inspectionReview=0;view.state=PreferencesOwnerState::Busy;
            result.status=PreferencesAdmission::Queued;result.ticket=view.submitted;wake.notify_one();return result;
        }catch(...){result.status=PreferencesAdmission::Invalid;return result;}
    }
    bool exchange(DuplexTaskProcess &process,const std::shared_ptr<const Job> &job,PreferencesReply *reply,LocalTaskStatus *failure){
        const auto began=job->admitted; // original admission; queue/encoding delay cannot renew this budget
        if(cancel.load()||!life.current()){*failure=life.current()?LocalTaskStatus::Cancelled:LocalTaskStatus::Unreaped;return false;}
        const auto initial=std::chrono::steady_clock::now()-began;
        if(initial<std::chrono::steady_clock::duration::zero()||initial>=std::chrono::milliseconds(policy.operationTimeoutMs)){*failure=LocalTaskStatus::Timeout;return false;}
        if(!process.send(job->wire)){*failure=LocalTaskStatus::Unavailable;return false;}
        std::string input;bool flushed=false;
        for(;;){
            if(cancel.load()||!life.current()){*failure=life.current()?LocalTaskStatus::Cancelled:LocalTaskStatus::Unreaped;return false;}
            const auto elapsed=std::chrono::steady_clock::now()-began;
            if(elapsed<std::chrono::steady_clock::duration::zero()||elapsed>=std::chrono::milliseconds(policy.operationTimeoutMs)){*failure=LocalTaskStatus::Timeout;return false;}
            const auto polled=process.poll();flushed=flushed||polled.flushed;
            if(polled.state!=DuplexTaskState::Running||polled.channelFault||polled.eof){*failure=polled.state==DuplexTaskState::Unreaped?LocalTaskStatus::Unreaped:LocalTaskStatus::Unavailable;return false;}
            if(polled.bytes.size()>preferencesFrameLimit-input.size()){*failure=LocalTaskStatus::Invalid;return false;}
            input+=polled.bytes;const auto end=input.find('\n');
            if(end!=input.npos){
                PreferencesReply value;
                if(!flushed||end+1!=input.size()||!decodePreferencesReply(input,&value)||value.ticket!=job->request.ticket||value.kind!=job->request.kind){*failure=LocalTaskStatus::Invalid;return false;}
                const auto op=job->request.operation;const auto status=value.status;
                const bool allowed=op==PreferencesOperation::Init?status==PreferencesReplyStatus::Ready:
                    op==PreferencesOperation::Load?(status==PreferencesReplyStatus::Loaded||status==PreferencesReplyStatus::Rejected):
                    (status==PreferencesReplyStatus::Saved||status==PreferencesReplyStatus::Rejected||status==PreferencesReplyStatus::Unknown);
                if(!allowed||(status==PreferencesReplyStatus::Saved&&value.document!=job->request.document)||
                   ((status==PreferencesReplyStatus::Loaded||status==PreferencesReplyStatus::Saved)&&!canonicalPreferencesDocument(value.kind,value.document,status==PreferencesReplyStatus::Loaded))){*failure=LocalTaskStatus::Invalid;return false;}
                const auto finished=std::chrono::steady_clock::now()-began;
                if(finished<std::chrono::steady_clock::duration::zero()||finished>=std::chrono::milliseconds(policy.operationTimeoutMs)){*failure=LocalTaskStatus::Timeout;return false;}
                if(cancel.load()||!life.current()){*failure=life.current()?LocalTaskStatus::Cancelled:LocalTaskStatus::Unreaped;return false;}
                *reply=std::move(value);return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    void fail(LocalTaskStatus status){retainWrite();cancel=true;queued.reset();view.reviews.fill(0);view.inspectionReview=0;view.transport=status;if(view.state!=PreferencesOwnerState::Stopped)view.state=PreferencesOwnerState::Faulted;wake.notify_all();}
    void run()noexcept {
        NativeProcessScope original(life.epoch());
        try {
            auto init=std::make_shared<Job>();init->admitted=started;init->request.ticket=1;init->request.operation=PreferencesOperation::Init;
            for(size_t i=0;i<8;++i)if(!targets[i].empty()){
                // Never collapse '..' through a possible symlink and silently select a different configured target.
                for(const auto &part:targets[i])if(part==".."){std::lock_guard<std::mutex> lock(mutex);fail(LocalTaskStatus::Invalid);view.settled=true;return;}
                const auto absolute=std::filesystem::absolute(targets[i]);
#ifndef _WIN32
                if(absolute.native().rfind("//",0)==0){std::lock_guard<std::mutex> lock(mutex);fail(LocalTaskStatus::Invalid);view.settled=true;return;}
#endif
                init->request.targets[i]=absolute.lexically_normal().u8string(); // only safe dot/separator normalization, background only
            }
            if(!encodePreferencesRequest(init->request,&init->wire)){std::lock_guard<std::mutex> lock(mutex);fail(LocalTaskStatus::Invalid);return;}
            DuplexTaskProcess process;
            if(!process.start(helper,cancel)){std::lock_guard<std::mutex> lock(mutex);fail(life.current()?LocalTaskStatus::Unavailable:LocalTaskStatus::Unreaped);return;}
            PreferencesReply ready;LocalTaskStatus failure=LocalTaskStatus::Unavailable;
            bool initialized=exchange(process,init,&ready,&failure);
            {std::lock_guard<std::mutex> lock(mutex);if(!initialized)fail(failure);else if(nativeCurrent()){view.state=PreferencesOwnerState::Ready;view.transport=LocalTaskStatus::Ok;view.completed=1;}}
            while(initialized){
                std::shared_ptr<const Job> job;
                {std::unique_lock<std::mutex> lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(10),[&]{return cancel.load()||static_cast<bool>(queued);});
                    if(!nativeCurrent())break;
                    if(!queued)continue;
                    inFlight=queued;queued.reset();job=inFlight;view.inFlight=true;}
                PreferencesReply response;failure=LocalTaskStatus::Unavailable;
                const bool ok=exchange(process,job,&response,&failure);
                std::shared_ptr<const PreferencesReply> published;if(ok)published=std::make_shared<const PreferencesReply>(std::move(response));
                {std::lock_guard<std::mutex> lock(mutex);
                    if(!ok){fail(failure);break;}
                    if(!nativeCurrent()){retainWrite();break;}
                    view.completed=job->request.ticket;view.reply=std::move(published);view.transport=LocalTaskStatus::Ok;
                    const auto index=static_cast<unsigned>(job->request.kind);
                    if(view.reply->status==PreferencesReplyStatus::Loaded||view.reply->status==PreferencesReplyStatus::Saved)view.reviews[index]=view.reply->review;
                    if(view.reply->status==PreferencesReplyStatus::Unknown)retainWrite();
                    if(view.uncertain&&view.reply->status==PreferencesReplyStatus::Loaded&&view.reply->kind==view.uncertain->kind)view.inspectionReview=view.reply->review;
                    inFlight.reset();view.pending=view.inFlight=false;view.state=view.uncertain?PreferencesOwnerState::NeedsInspection:PreferencesOwnerState::Ready;
                }
            }
            process.requestStop();const bool settled=process.terminate(policy.reapMs);
            {std::lock_guard<std::mutex> lock(mutex);if(!settled)fail(LocalTaskStatus::Unreaped);view.settled=settled;inFlight.reset();view.pending=view.inFlight=false;}
        }catch(...){try{std::lock_guard<std::mutex> lock(mutex);fail(life.current()?LocalTaskStatus::Unavailable:LocalTaskStatus::Unreaped);inFlight.reset();view.pending=view.inFlight=false;}catch(...) {}}
    }
    void stopUnconditionally(){std::lock_guard<std::mutex> lock(mutex);revoke(PreferencesOwnerState::Stopped);}
};
PreferencesOwner::PreferencesOwner(std::filesystem::path helper,std::array<std::filesystem::path,8> targets,const SessionIntent &intent,SessionToken token,PreferencesOwnerPolicy policy):impl_(new Impl(std::move(helper),std::move(targets),intent,token,policy)){}
PreferencesOwner::~PreferencesOwner(){auto &s=*impl_;std::lock_guard<std::mutex> lifecycle(s.lifecycle);s.stopUnconditionally();if(s.thread.joinable())s.thread.join();}
bool PreferencesOwner::start(){auto &s=*impl_;std::lock_guard<std::mutex> lifecycle(s.lifecycle);std::lock_guard<std::mutex> lock(s.mutex);
    if(s.foreign()||s.attempted||s.view.state!=PreferencesOwnerState::Dormant||!s.intent.permits(s.token)||s.policy.operationTimeoutMs<50||s.policy.operationTimeoutMs>60000||!s.policy.reapMs||s.policy.reapMs>1000||!localTaskPath(s.helper))return false;
    bool any=false;for(const auto &path:s.targets){if(path.native().size()>4096)return false;any=any||!path.empty();}if(!any||!s.life.begin())return false;
    s.attempted=true;s.cancel=false;s.started=std::chrono::steady_clock::now();s.view.state=PreferencesOwnerState::Starting;s.view.submitted=1;
    try{s.thread=std::thread([p=&s]{p->run();});return true;}catch(...){s.fail(LocalTaskStatus::Unavailable);return false;}
}
PreferencesSubmission PreferencesOwner::load(PreferencesKind kind){auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);return s.enqueue(PreferencesOperation::Load,kind,0,{});}
PreferencesSubmission PreferencesOwner::save(PreferencesKind kind,uint64_t review,const std::string &document){auto &s=*impl_;
    {std::lock_guard<std::mutex> lock(s.mutex);if(s.foreign()||!s.current())return {};}
    try{if(!canonicalPreferencesDocument(kind,document))return {PreferencesAdmission::Invalid,0};}
    catch(...){return {PreferencesAdmission::Invalid,0};}
    std::lock_guard<std::mutex> lock(s.mutex);return s.enqueue(PreferencesOperation::Save,kind,review,document);
}
std::shared_ptr<const PreferencesReply> PreferencesOwner::takeReply(uint64_t ticket){auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);if(s.foreign()||!s.current()||!s.view.reply||s.view.reply->ticket!=ticket)return {};auto reply=s.view.reply;s.view.reply.reset();return reply;}
bool PreferencesOwner::acknowledgeInspection(PreferencesKind kind,uint64_t review){auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);
    const auto index=static_cast<unsigned>(kind);
    if(s.foreign()||!s.current()||index>=8||!review||!s.view.uncertain||s.view.uncertain->kind!=kind||s.view.pending||s.view.reply||s.view.inspectionReview!=review||s.view.reviews[index]!=review)return false;
    s.view.acknowledgedUnknown=s.view.uncertain;s.view.uncertain.reset();s.view.inspectionReview=0;s.view.state=PreferencesOwnerState::Ready;return true;
}
PreferencesOwnerSnapshot PreferencesOwner::snapshot()const{auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);if(s.foreign())return s.denied;s.current();return s.view;}
void PreferencesOwner::requestStop(){auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);if(s.foreign())return;s.revoke(PreferencesOwnerState::Stopped);}
void PreferencesOwner::stop(){auto &s=*impl_;std::lock_guard<std::mutex> lifecycle(s.lifecycle);{std::lock_guard<std::mutex> lock(s.mutex);if(s.foreign())return;s.revoke(PreferencesOwnerState::Stopped);}if(s.thread.joinable())s.thread.join();}
} // namespace om
