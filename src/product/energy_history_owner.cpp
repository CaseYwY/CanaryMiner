#include "energy_history_owner.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace om {
namespace {
int64_t utcNow(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
bool accepted(EnergyHistoryStatus s){return s==EnergyHistoryStatus::Ok||s==EnergyHistoryStatus::Replay;}
}
struct EnergyHistoryOwner::Impl {
    struct Job { EnergyHistoryRequest request;bool retry=false; };
    EnergyHistoryProcessClient client;
    std::filesystem::path file;
    EnergyHistoryBinding binding;
    EnergyHistoryLimits limits;
    unsigned timeout;
    EnergyHistoryClock clock;
    mutable std::mutex mutex;
    std::mutex lifecycle;
    std::condition_variable wake;
    std::thread thread;
    std::atomic<bool> cancel{true};
    std::shared_ptr<const Job> queued,inFlight;
    std::shared_ptr<const EnergyHistoryBatch> uncertain;
    EnergyHistoryOwnerSnapshot current;
    std::shared_ptr<const EnergyHistoryOwnerSnapshot> published=std::make_shared<const EnergyHistoryOwnerSnapshot>();
    uint64_t greatestGeneration=0;
    int64_t lastUtc=0;
    std::string ownedSession;
    Impl(std::filesystem::path helper,std::filesystem::path path,EnergyHistoryBinding b,EnergyHistoryLimits l,unsigned ms,EnergyHistoryClock c)
        :client(std::move(helper)),file(std::move(path)),binding(std::move(b)),limits(l),timeout(ms),clock(c?std::move(c):EnergyHistoryClock(utcNow)){}
    void publish(){current.pending=static_cast<bool>(queued||inFlight);current.uncertainBatch=uncertain;++current.revision;published=std::make_shared<const EnergyHistoryOwnerSnapshot>(current);}
    void retain(const std::shared_ptr<const Job> &job){
        if(!job)return;
        if(job->request.batch)uncertain=std::shared_ptr<const EnergyHistoryBatch>(job,&*job->request.batch);
        if(job->request.mode==EnergyHistoryOpen::CreateNew)current.creationOutcomeUnknown=true;
    }
    EnergyHistoryAdmission check(uint64_t generation,uint64_t revision){
        if(generation!=current.generation)return EnergyHistoryAdmission::StaleGeneration;
        if(!revision||revision!=current.revision)return EnergyHistoryAdmission::ReviewChanged;
        if(cancel||queued||inFlight)return EnergyHistoryAdmission::NotReady;
        return EnergyHistoryAdmission::Queued;
    }
    EnergyHistoryAdmission enqueue(const EnergyHistoryBatch *b,const std::vector<EnergyHistoryQuery> &queries,bool retry,EnergyHistoryOpen mode=EnergyHistoryOpen::Existing,bool initial=false){
        if(queued||inFlight||queries.size()>4||current.lastTicket==UINT64_MAX)return EnergyHistoryAdmission::NotReady;
        for(const auto &q:queries)if(q.registerId.size()>128||q.periodId.size()>128)return EnergyHistoryAdmission::Invalid; // bound borrowed strings before copying
        const auto now=clock();if(now<lastUtc)return EnergyHistoryAdmission::Invalid;
        if(b&&!boundedEnergyHistoryBatch(binding,*b,now))return EnergyHistoryAdmission::Invalid;
        auto job=std::make_shared<Job>();auto &r=job->request;
        r.generation=current.generation;r.ticket=current.lastTicket+1;r.file=file;r.binding=binding;r.limits=limits;r.mode=mode;r.nowUtcMs=now;r.queries=queries;
        if(b)r.batch=*b;
        job->retry=retry;std::string wire;if(!encodeEnergyHistoryRequest(r,&wire))return EnergyHistoryAdmission::Invalid;
        const auto oldState=current.state;const auto oldUtc=lastUtc;
        queued=job;current.lastTicket=r.ticket;lastUtc=now;current.state=initial?EnergyHistoryOwnerState::Starting:EnergyHistoryOwnerState::Busy;
        try{publish();}catch(...){queued.reset();--current.lastTicket;lastUtc=oldUtc;current.state=oldState;throw;}
        wake.notify_one();return EnergyHistoryAdmission::Queued;
    }
    void run() noexcept {
        try{
            for(;;){std::shared_ptr<const Job> job;
                {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return cancel.load()||queued;});if(cancel)return;
                    inFlight=queued;queued.reset();job=inFlight;}
                auto result=client.transact(job->request,cancel,timeout);
                std::lock_guard<std::mutex> lock(mutex);
                if(cancel){inFlight.reset();return;}
                current.transport=result.transport;current.reply=std::move(result.reply);
                bool ok=current.transport==LocalTaskStatus::Ok&&current.reply&&current.reply->opened==EnergyHistoryStatus::Ok&&current.reply->checkpoint.status==EnergyHistoryStatus::Ok;
                if(current.reply){if(current.reply->applied&&!accepted(*current.reply->applied))ok=false;
                    for(const auto &t:current.reply->totals)if(t.status!=EnergyHistoryStatus::Ok)ok=false;}
                if(result.writeOutcomeUnknown)retain(job);
                if(ok&&job->retry)uncertain.reset();
                if(ok){
                    current.creationOutcomeUnknown=false;
                    // After an explicit generation restart, a recovered old session is never resumed implicitly.
                    if(job->request.batch&&job->request.batch->start&&!job->retry)ownedSession=job->request.batch->sessionId;
                    current.state=uncertain?EnergyHistoryOwnerState::NeedsReview:EnergyHistoryOwnerState::Ready;
                }else{current.state=EnergyHistoryOwnerState::Faulted;cancel=true;}
                inFlight.reset();publish();if(!ok)return;
            }
        }catch(...){try{std::lock_guard<std::mutex> lock(mutex);if(cancel)return;retain(inFlight);queued.reset();inFlight.reset();cancel=true;current.state=EnergyHistoryOwnerState::Faulted;publish();}catch(...){cancel=true;}}
    }
};
EnergyHistoryOwner::EnergyHistoryOwner(std::filesystem::path helper,std::filesystem::path file,EnergyHistoryBinding binding,EnergyHistoryLimits limits,unsigned timeout,EnergyHistoryClock clock)
    :impl_(new Impl(std::move(helper),std::move(file),std::move(binding),limits,timeout,std::move(clock))){}
EnergyHistoryOwner::~EnergyHistoryOwner(){stop();}
bool EnergyHistoryOwner::start(uint64_t generation,EnergyHistoryOpen mode){
    auto &s=*impl_;std::lock_guard<std::mutex> life(s.lifecycle);std::lock_guard<std::mutex> lock(s.mutex);
    if(s.thread.joinable()||!generation||generation<=s.greatestGeneration||!validEnergyHistoryBinding(s.binding)||!validEnergyHistoryLimits(s.limits)||!localTaskPath(s.file)||s.timeout<50||s.timeout>60000||
        (mode==EnergyHistoryOpen::CreateNew&&(s.uncertain||s.current.creationOutcomeUnknown)))return false;
    try{
        s.greatestGeneration=generation;const bool creation=s.current.creationOutcomeUnknown;s.current={};s.current.creationOutcomeUnknown=creation;s.current.generation=generation;
        s.cancel=false;s.lastUtc=0;s.ownedSession.clear();
        if(s.enqueue(nullptr,{},false,mode,true)!=EnergyHistoryAdmission::Queued){s.cancel=true;s.current.state=EnergyHistoryOwnerState::Faulted;s.publish();return false;}
        s.thread=std::thread([p=&s]{p->run();});return true;
    }catch(...){s.cancel=true;s.queued.reset();s.current.state=EnergyHistoryOwnerState::Faulted;try{s.publish();}catch(...){}return false;}
}
EnergyHistoryAdmission EnergyHistoryOwner::submit(uint64_t generation,uint64_t revision,const EnergyHistoryBatch &batch,const std::vector<EnergyHistoryQuery> &queries){
    auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);const auto checked=s.check(generation,revision);if(checked!=EnergyHistoryAdmission::Queued)return checked;
    if(s.current.state!=EnergyHistoryOwnerState::Ready||s.uncertain||!s.current.reply||s.current.reply->checkpoint.status!=EnergyHistoryStatus::Ok)return EnergyHistoryAdmission::NotReady;
    if(batch.sequence!=s.current.reply->checkpoint.sequence+1||(!batch.start&&!batch.readings.empty()&&batch.sessionId!=s.ownedSession))return EnergyHistoryAdmission::Invalid;
    try{return s.enqueue(&batch,queries,false);}catch(...){return EnergyHistoryAdmission::Invalid;}
}
EnergyHistoryAdmission EnergyHistoryOwner::refresh(uint64_t generation,uint64_t revision,const std::vector<EnergyHistoryQuery> &queries){
    auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);const auto checked=s.check(generation,revision);if(checked!=EnergyHistoryAdmission::Queued)return checked;
    if(s.current.state!=EnergyHistoryOwnerState::Ready&&s.current.state!=EnergyHistoryOwnerState::NeedsReview)return EnergyHistoryAdmission::NotReady;
    try{return s.enqueue(nullptr,queries,false);}catch(...){return EnergyHistoryAdmission::Invalid;}
}
EnergyHistoryAdmission EnergyHistoryOwner::retryReviewed(uint64_t generation,uint64_t revision,const std::vector<EnergyHistoryQuery> &queries){
    auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);const auto checked=s.check(generation,revision);if(checked!=EnergyHistoryAdmission::Queued)return checked;
    if(s.current.state!=EnergyHistoryOwnerState::NeedsReview||!s.uncertain)return EnergyHistoryAdmission::NotReady;
    try{return s.enqueue(s.uncertain.get(),queries,true);}catch(...){return EnergyHistoryAdmission::Invalid;}
}
void EnergyHistoryOwner::requestStop(){
    auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);s.cancel=true;s.retain(s.inFlight);s.queued.reset();s.current.state=EnergyHistoryOwnerState::Stopped;
    try{s.publish();}catch(...){}s.wake.notify_all();
}
void EnergyHistoryOwner::stop(){
    auto &s=*impl_;std::lock_guard<std::mutex> life(s.lifecycle);requestStop();if(s.thread.joinable())s.thread.join();
    std::lock_guard<std::mutex> lock(s.mutex);s.inFlight.reset();try{s.publish();}catch(...){}
}
std::shared_ptr<const EnergyHistoryOwnerSnapshot> EnergyHistoryOwner::snapshot()const{std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->published;}
} // namespace om
