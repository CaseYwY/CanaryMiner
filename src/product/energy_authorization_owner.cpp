#include "energy_authorization_owner.h"
#include "energy_history_model.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace om {
namespace {
using A=EnergyAuthorizationAction;
using S=EnergyAuthorizationStatus;
using O=EnergyAuthorizationOwnerState;
int64_t nowUtc(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
bool mutation(A a){return a==A::Save||a==A::Enroll||a==A::Confirm||a==A::Provision;}
}
struct EnergyAuthorizationOwner::Impl {
    EnergyAuthorizationProcessClient client;
    std::filesystem::path file;
    unsigned timeout;
    EnergyHistoryClock clock;
    mutable std::mutex mutex;
    std::mutex lifecycle;
    std::condition_variable wake;
    std::thread thread;
    std::atomic<bool> cancel{true};
    std::shared_ptr<const EnergyAuthorizationRequest> queued,inFlight;
    std::optional<EnergyAuthorizedCapture> plan;
    EnergyAuthorizationSnapshot current;
    std::shared_ptr<const EnergyAuthorizationSnapshot> published=std::make_shared<const EnergyAuthorizationSnapshot>();
    bool attempted=false;
    int64_t lastUtc=0;
    Impl(std::filesystem::path helper,std::filesystem::path path,bool disabled,unsigned ms,EnergyHistoryClock c)
        :client(std::move(helper)),file(std::move(path)),timeout(ms),clock(c?std::move(c):EnergyHistoryClock(nowUtc)){current.cliNoEnergy=disabled;}
    void publish(){current.pending=static_cast<bool>(queued||inFlight);current.planAvailable=plan.has_value();++current.revision;published=std::make_shared<const EnergyAuthorizationSnapshot>(current);}
    void retain(){if(inFlight&&mutation(inFlight->action)&&!current.uncertain)current.uncertain=inFlight;}
    EnergyHistoryAdmission check(uint64_t g,uint64_t r){
        if(g!=current.generation)return EnergyHistoryAdmission::StaleGeneration;
        if(!r||r!=current.revision)return EnergyHistoryAdmission::ReviewChanged;
        if(cancel||queued||inFlight)return EnergyHistoryAdmission::NotReady;
        return EnergyHistoryAdmission::Queued;
    }
    EnergyHistoryAdmission enqueue(const EnergyAuthorizationCommand &command){
        if(current.lastTicket==UINT64_MAX||command.devices.size()>64)return EnergyHistoryAdmission::Invalid;
        for(const auto &id:command.devices)if(id.size()>40)return EnergyHistoryAdmission::Invalid;
        const auto now=clock();
        if(!energy_history_detail::instant(now)||now<lastUtc){cancel=true;plan.reset();current.state=O::Faulted;publish();wake.notify_all();return EnergyHistoryAdmission::Invalid;}
        auto r=std::make_shared<EnergyAuthorizationRequest>();r->generation=current.generation;r->ticket=current.lastTicket+1;r->file=file;r->action=command.action;
        r->nowUtcMs=now;r->policy=command.policy;r->limits=command.limits;r->devices=command.devices;
        if(command.action!=A::Load){if(!current.reply||!current.reply->document)return EnergyHistoryAdmission::NotReady;r->expected=current.reply->token;}
        std::string wire;if(!encodeEnergyAuthorizationRequest(*r,&wire))return EnergyHistoryAdmission::Invalid;
        auto next=current;next.lastTicket=r->ticket;next.state=O::Busy;next.inspectionAvailable=false;next.pending=true;next.planAvailable=false;++next.revision;
        // A new settings action cannot make an old, unconsumed issuance look current.
        if(command.action==A::Provision)next.provisionAttempted=true;
        // Allocate the published snapshot before committing admission: a failed allocation must not leave a hidden job.
        auto snapshot=std::make_shared<const EnergyAuthorizationSnapshot>(next);
        queued=std::move(r);lastUtc=now;plan.reset();current=std::move(next);published=std::move(snapshot);
        wake.notify_one();return EnergyHistoryAdmission::Queued;
    }
    void run()noexcept{
        try{
            for(;;){std::shared_ptr<const EnergyAuthorizationRequest> job;
                {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return cancel.load()||queued;});if(cancel)return;
                    inFlight=queued;queued.reset();job=inFlight;}
                auto result=client.transact(*job,cancel,timeout);
                std::lock_guard<std::mutex> lock(mutex);
                if(cancel){inFlight.reset();return;}
                current.transport=result.transport;current.reply=std::move(result.reply);
                if(result.writeOutcomeUnknown)retain();
                const bool readable=current.reply&&(current.reply->status==S::Ok||current.reply->status==S::Missing);
                if(result.transport==LocalTaskStatus::Unreaped){current.state=O::Faulted;cancel=true;}
                else if(current.uncertain){current.state=O::NeedsReview;current.inspectionAvailable=job->action==A::Load&&readable;}
                else if(!current.reply||current.reply->status==S::Storage||current.reply->status==S::Clock){current.state=O::Faulted;cancel=true;}
                else{
                    current.state=current.reply->status==S::NeedsReview?O::NeedsReview:O::Ready;
                    if(readable&&current.reply->sessionIssued&&!current.cliNoEnergy){
                        const auto &d=*current.reply->document;EnergyAuthorizedCapture issued;issued.file=energyAuthorizationStorePath(file,d);
                        issued.plan.policy=d.policy;issued.plan.binding=d.binding;issued.plan.limits=d.limits;issued.plan.privateNamespace=d.privateNamespace;
                        issued.plan.sessionId=d.lastSessionId;issued.plan.mode=EnergyHistoryOpen::Existing;plan=std::move(issued);
                    }
                }
                inFlight.reset();publish();if(cancel)return;
            }
        }catch(...){try{std::lock_guard<std::mutex> lock(mutex);if(cancel)return;retain();queued.reset();inFlight.reset();plan.reset();cancel=true;current.state=O::Faulted;publish();}catch(...){cancel=true;}}
    }
};
EnergyAuthorizationOwner::EnergyAuthorizationOwner(std::filesystem::path helper,std::filesystem::path file,bool disabled,unsigned timeout,EnergyHistoryClock clock)
    :impl_(new Impl(std::move(helper),std::move(file),disabled,timeout,std::move(clock))){}
EnergyAuthorizationOwner::~EnergyAuthorizationOwner(){stop();}
bool EnergyAuthorizationOwner::start(uint64_t generation){
    auto &s=*impl_;std::lock_guard<std::mutex> life(s.lifecycle);std::lock_guard<std::mutex> lock(s.mutex);
    if(s.attempted)return false;
    s.attempted=true;if(!generation||!localTaskPath(s.file)||s.timeout<50||s.timeout>60000)return false;
    try{s.current.generation=generation;s.cancel=false;
        if(s.enqueue({})!=EnergyHistoryAdmission::Queued)return false;
        s.thread=std::thread([p=&s]{p->run();});return true;
    }catch(...){s.cancel=true;s.queued.reset();s.current.state=O::Faulted;try{s.publish();}catch(...){}return false;}
}
EnergyHistoryAdmission EnergyAuthorizationOwner::submit(uint64_t generation,uint64_t revision,const EnergyAuthorizationCommand &command){
    auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);const auto checked=s.check(generation,revision);if(checked!=EnergyHistoryAdmission::Queued)return checked;
    if(s.current.uncertain&&command.action!=A::Load)return EnergyHistoryAdmission::NotReady;
    if(command.action==A::Provision&&(s.current.cliNoEnergy||s.current.provisionAttempted))return EnergyHistoryAdmission::NotReady;
    try{return s.enqueue(command);}catch(...){return EnergyHistoryAdmission::Invalid;}
}
EnergyHistoryAdmission EnergyAuthorizationOwner::acknowledgeInspection(uint64_t generation,uint64_t revision){
    auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);const auto checked=s.check(generation,revision);if(checked!=EnergyHistoryAdmission::Queued)return checked;
    if(!s.current.uncertain||!s.current.inspectionAvailable)return EnergyHistoryAdmission::NotReady;
    s.current.uncertain.reset();s.current.inspectionAvailable=false;s.current.state=O::Ready;s.publish();return EnergyHistoryAdmission::Queued;
}
std::optional<EnergyAuthorizedCapture> EnergyAuthorizationOwner::takePlan(uint64_t generation,uint64_t revision){
    auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);
    if(s.check(generation,revision)!=EnergyHistoryAdmission::Queued||s.current.cliNoEnergy||s.current.uncertain||!s.plan)return {};
    auto result=std::move(s.plan);s.plan.reset();s.publish();return result;
}
void EnergyAuthorizationOwner::requestStop(){
    auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);s.attempted=true;s.cancel=true;s.retain();s.queued.reset();s.plan.reset();s.current.state=O::Stopped;
    try{s.publish();}catch(...){}s.wake.notify_all();
}
void EnergyAuthorizationOwner::stop(){auto &s=*impl_;std::lock_guard<std::mutex> life(s.lifecycle);requestStop();if(s.thread.joinable())s.thread.join();
    std::lock_guard<std::mutex> lock(s.mutex);s.inFlight.reset();try{s.publish();}catch(...){} }
std::shared_ptr<const EnergyAuthorizationSnapshot> EnergyAuthorizationOwner::snapshot()const{std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->published;}
} // namespace om
