#include "energy_leased_capture.h"
#include "energy_collector_process.h"
#include <chrono>
#include <mutex>
#include <thread>
namespace om {
namespace {
int64_t nowUtc(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
class LeasedCapture final:public EnergyCaptureSink {
    std::filesystem::path authorizationHelper_,collectorHelper_,file_;
    bool disabled_;
    unsigned timeout_;
    EnergyHistoryClock clock_;
    mutable std::mutex mutex_;
    std::mutex lifecycle_;
    std::thread thread_;
    std::atomic<bool> cancel_{true};
    bool attempted_=false,stopped_=true,leaseHeld_=false,provisionPending_=false;
    uint64_t innerRevision_=0,startupDropped_=0;
    std::unique_ptr<EnergyCaptureSink> capture_;
    EnergyCaptureSnapshot current_;
    std::shared_ptr<const EnergyCaptureSnapshot> published_=std::make_shared<const EnergyCaptureSnapshot>();
    void publish(){++current_.revision;published_=std::make_shared<const EnergyCaptureSnapshot>(current_);}
    void sync(){
        if(capture_){const auto s=capture_->snapshot();if(s->revision!=innerRevision_){
                innerRevision_=s->revision;const auto lease=current_.leaseState;const auto transport=current_.leaseTransport;const auto unknown=current_.provisioningOutcomeUnknown;
                const auto original=current_.provisionRequest;const auto rev=current_.revision;current_=*s;current_.revision=rev;current_.leaseState=lease;current_.leaseTransport=transport;current_.provisioningOutcomeUnknown=unknown;current_.provisionRequest=original;
                current_.dropped+=startupDropped_;
                if(stopped_)current_.state=current_.leaseState==EnergyLeaseState::Lost?EnergyCaptureState::Faulted:EnergyCaptureState::Stopped;
                publish();}}
    }
    void lost(LocalTaskStatus transport,bool unknown){
        leaseHeld_=false;stopped_=true;cancel_=true;current_.leaseState=EnergyLeaseState::Lost;current_.leaseTransport=transport;current_.provisioningOutcomeUnknown=unknown;
        if(capture_)capture_->requestStop();
        sync();current_.state=EnergyCaptureState::Faulted;publish();
    }
    void run(uint64_t generation,std::vector<std::string> devices)noexcept{
        bool issued=false,started=false;
        try{
            EnergyAuthorizationProcessClient client(authorizationHelper_);EnergyAuthorizationRequest load;
            load.generation=generation;load.ticket=1;load.file=file_;load.nowUtcMs=clock_();
            const auto result=client.transact(load,cancel_,timeout_);
            {std::lock_guard<std::mutex> lock(mutex_);if(cancel_)return;
                if(result.reply&&(result.reply->status==EnergyAuthorizationStatus::Missing||(result.reply->status==EnergyAuthorizationStatus::Ok&&result.reply->document&&!result.reply->document->policy.persistenceAuthorized))){
                    stopped_=true;cancel_=true;current_.state=EnergyCaptureState::Stopped;current_.leaseState=EnergyLeaseState::Disabled;current_.leaseTransport=result.transport;publish();return;}
                if(!result.reply||result.reply->status!=EnergyAuthorizationStatus::Ok||!result.reply->document){lost(result.transport,false);return;}}
            EnergyCollectorLeaseRequest r;r.parentPid=localTaskProcessId();r.provision=load;r.provision.ticket=2;r.provision.action=EnergyAuthorizationAction::Provision;
            r.provision.expected=result.reply->token;r.provision.devices=devices;r.provision.nowUtcMs=clock_();
            std::string wire;if(r.provision.nowUtcMs<load.nowUtcMs||!encodeEnergyCollectorLeaseRequest(r,&wire)){
                std::lock_guard<std::mutex> lock(mutex_);if(!cancel_)lost(LocalTaskStatus::Invalid,false);return;}
            LocalTaskProcess process(collectorHelper_);
            {std::lock_guard<std::mutex> lock(mutex_);if(cancel_)return;
                current_.provisionRequest=std::make_shared<const EnergyAuthorizationRequest>(r.provision);provisionPending_=true;}
            const auto held=process.hold(wire,cancel_,timeout_,[&](const std::string &frame){
                EnergyCollectorLeaseReply reply;if(!decodeEnergyCollectorLeaseReply(r,frame,&reply))return false;
                std::lock_guard<std::mutex> lock(mutex_);if(cancel_)return false;
                issued=true;provisionPending_=false;current_.provisionRequest.reset();
                const auto &d=*reply.provision.document;EnergyCapturePlan plan;plan.policy=d.policy;plan.binding=d.binding;plan.limits=d.limits;plan.privateNamespace=d.privateNamespace;
                plan.sessionId=d.lastSessionId;plan.mode=EnergyHistoryOpen::Existing;plan.fence=EnergyCollectorFence{file_,reply.epoch};
                capture_=makeEnergyCapture(collectorHelper_,energyAuthorizationStorePath(file_,d),std::move(plan),timeout_,clock_);
                if(!capture_||!capture_->start(generation,devices))return false;
                leaseHeld_=true;current_.leaseState=EnergyLeaseState::Held;current_.leaseTransport=LocalTaskStatus::Ok;sync();publish();return true;
            });
            started=held.childStarted;
            std::lock_guard<std::mutex> lock(mutex_);if(cancel_)return;
            provisionPending_=false;if(!started)current_.provisionRequest.reset();lost(held.status,started&&!issued);
        }catch(...){try{std::lock_guard<std::mutex> lock(mutex_);if(!cancel_)lost(LocalTaskStatus::Unavailable,provisionPending_||(started&&!issued));}catch(...){cancel_=true;}}
    }
   public:
    LeasedCapture(std::filesystem::path auth,std::filesystem::path collector,std::filesystem::path file,bool disabled,unsigned timeout,EnergyHistoryClock clock)
        :authorizationHelper_(std::move(auth)),collectorHelper_(std::move(collector)),file_(std::move(file)),disabled_(disabled),timeout_(timeout),clock_(clock?std::move(clock):EnergyHistoryClock(nowUtc)){}
    ~LeasedCapture()override{stop();}
    bool start(uint64_t generation,const std::vector<std::string> &devices)override{
        std::lock_guard<std::mutex> life(lifecycle_);std::lock_guard<std::mutex> lock(mutex_);if(attempted_)return false;attempted_=true;
        EnergyHistoryBinding dummy;
        if(disabled_||!generation||timeout_<50||timeout_>60000||!localTaskPath(file_)||!localTaskPath(authorizationHelper_)||!localTaskPath(collectorHelper_)||
            !makeEnergyCaptureBinding(std::string(32,'0'),1,devices,PowerOrigin::ObservedTelemetry,&dummy))return false;
        try{cancel_=false;stopped_=false;current_.generation=generation;current_.state=EnergyCaptureState::Starting;current_.leaseState=EnergyLeaseState::Starting;publish();
            thread_=std::thread([this,generation,devices]{run(generation,devices);});return true;
        }catch(...){cancel_=true;stopped_=true;current_.state=EnergyCaptureState::Faulted;current_.leaseState=EnergyLeaseState::Lost;try{publish();}catch(...){}return false;}
    }
    EnergyCaptureAdmission submit(uint64_t generation,const SessionPowerInput &input)override{
        std::lock_guard<std::mutex> lock(mutex_);
        if(generation!=current_.generation)return EnergyCaptureAdmission::StaleGeneration;
        if(stopped_)return EnergyCaptureAdmission::Stopped;
        if(!leaseHeld_||!capture_){++startupDropped_;current_.dropped=startupDropped_;publish();return EnergyCaptureAdmission::NotReady;}
        const auto result=capture_->submit(generation,input);sync();return result;
    }
    void markGap(uint64_t generation)override{std::lock_guard<std::mutex> lock(mutex_);if(!stopped_&&leaseHeld_&&capture_){capture_->markGap(generation);sync();}}
    void tick(int64_t utc,uint64_t mono)override{std::lock_guard<std::mutex> lock(mutex_);if(!stopped_&&leaseHeld_&&capture_){capture_->tick(utc,mono);sync();}}
    EnergyHistoryAdmission inspect(uint64_t generation,uint64_t revision)override{std::lock_guard<std::mutex> lock(mutex_);if(stopped_||!leaseHeld_||!capture_)return EnergyHistoryAdmission::NotReady;
        const auto result=capture_->inspect(generation,revision);sync();return result;}
    EnergyHistoryAdmission retryReviewed(uint64_t generation,uint64_t revision)override{std::lock_guard<std::mutex> lock(mutex_);if(stopped_||!leaseHeld_||!capture_)return EnergyHistoryAdmission::NotReady;
        const auto result=capture_->retryReviewed(generation,revision);sync();return result;}
    void requestStop()override{std::lock_guard<std::mutex> lock(mutex_);attempted_=true;if(current_.leaseState==EnergyLeaseState::Stopped)return;
        stopped_=true;leaseHeld_=false;cancel_=true;current_.leaseState=EnergyLeaseState::Stopped;
        if(provisionPending_)current_.provisioningOutcomeUnknown=true;
        if(capture_)capture_->requestStop();
        try{sync();current_.state=EnergyCaptureState::Stopped;publish();}catch(...){} }
    void stop()override{std::lock_guard<std::mutex> life(lifecycle_);requestStop();if(thread_.joinable())thread_.join();if(capture_)capture_->stop();}
    std::shared_ptr<const EnergyCaptureSnapshot> snapshot()const override{std::lock_guard<std::mutex> lock(mutex_);return published_;}
};
}
std::unique_ptr<EnergyCaptureSink> makeLeasedEnergyCapture(std::filesystem::path auth,std::filesystem::path collector,std::filesystem::path file,bool disabled,unsigned timeout,EnergyHistoryClock clock){
    return std::make_unique<LeasedCapture>(std::move(auth),std::move(collector),std::move(file),disabled,timeout,std::move(clock));
}
} // namespace om
