#include "energy_runtime.h"
#include "energy_leased_capture.h"
#include <mutex>

namespace om {
namespace {
class Runtime final:public EnergyRuntime {
    std::filesystem::path authorizationHelper_,collectorHelper_,file_;
    bool mining_,disabled_,attempted_=false,stopped_=true,authorizationStarted_=false;
    unsigned timeout_;
    EnergyHistoryClock clock_;
    mutable std::mutex mutex_;
    std::mutex lifecycle_;
    uint64_t generation_=0;
    std::vector<std::string> selected_;
    EnergyAuthorizationOwner authorization_;
    std::unique_ptr<EnergyCaptureSink> capture_;
    std::shared_ptr<const EnergyCaptureSnapshot> captureView_=std::make_shared<const EnergyCaptureSnapshot>();
    std::shared_ptr<const EnergyAuthorizationSnapshot> authorizationView_=std::make_shared<const EnergyAuthorizationSnapshot>();

    void beginAuthorization(){
        if(authorizationStarted_||stopped_)return;
        // The capture startup may write Provision. Do not expose an editable pre-Provision CAS snapshot.
        if(capture_&&capture_->snapshot()->leaseState==EnergyLeaseState::Starting)return;
        authorizationStarted_=true;
        authorization_.start(generation_); // one read AFTER the frozen startup decision; never Provision here
    }
   public:
    Runtime(std::filesystem::path auth,std::filesystem::path collector,std::filesystem::path file,bool mining,bool disabled,unsigned timeout,EnergyHistoryClock clock)
        :authorizationHelper_(std::move(auth)),collectorHelper_(std::move(collector)),file_(std::move(file)),mining_(mining),disabled_(disabled),
         timeout_(timeout),clock_(std::move(clock)),authorization_(authorizationHelper_,file_,disabled_,timeout_,clock_){}
    ~Runtime()override{stop();}
    bool start(uint64_t generation,const std::vector<std::string> &devices)override{
        std::lock_guard<std::mutex> life(lifecycle_);std::lock_guard<std::mutex> lock(mutex_);
        if(attempted_)return false;
        attempted_=true;
        if(!generation||timeout_<50||timeout_>60000||!localTaskPath(file_)||!localTaskPath(authorizationHelper_)||!localTaskPath(collectorHelper_))return false;
        generation_=generation;stopped_=false;
        EnergyHistoryBinding dummy;
        if(makeEnergyCaptureBinding(std::string(32,'0'),1,devices,PowerOrigin::ObservedTelemetry,&dummy))selected_=devices;
        EnergyCaptureSnapshot c;c.generation=generation;c.revision=1;c.leaseState=EnergyLeaseState::Disabled;
        if(mining_&&!disabled_){
            if(selected_.empty()){c.state=EnergyCaptureState::Faulted;c.leaseState=EnergyLeaseState::Lost;}
            else{
                capture_=makeLeasedEnergyCapture(authorizationHelper_,collectorHelper_,file_,false,timeout_,clock_);
                if(!capture_->start(generation,selected_)){capture_.reset();c.state=EnergyCaptureState::Faulted;c.leaseState=EnergyLeaseState::Lost;}
            }
        }
        captureView_=std::make_shared<const EnergyCaptureSnapshot>(c);
        EnergyAuthorizationSnapshot a;a.generation=generation;a.revision=1;a.state=EnergyAuthorizationOwnerState::Busy;
        a.pending=a.startupPending=true;a.cliNoEnergy=disabled_;authorizationView_=std::make_shared<const EnergyAuthorizationSnapshot>(a);
        beginAuthorization();return true;
    }
    EnergyCaptureAdmission submit(uint64_t generation,const SessionPowerInput &input)override{
        std::lock_guard<std::mutex> lock(mutex_);
        if(generation!=generation_)return EnergyCaptureAdmission::StaleGeneration;
        if(stopped_||!capture_)return EnergyCaptureAdmission::Stopped;
        return capture_->submit(generation,input);
    }
    void markGap(uint64_t generation)override{std::lock_guard<std::mutex> lock(mutex_);if(!stopped_&&capture_)capture_->markGap(generation);}
    void tick(int64_t utc,uint64_t mono)override{
        std::lock_guard<std::mutex> lock(mutex_);if(stopped_)return;
        if(capture_)capture_->tick(utc,mono);
        beginAuthorization();
    }
    EnergyHistoryAdmission inspect(uint64_t generation,uint64_t revision)override{
        std::lock_guard<std::mutex> lock(mutex_);return !stopped_&&capture_?capture_->inspect(generation,revision):EnergyHistoryAdmission::NotReady;
    }
    EnergyHistoryAdmission retryReviewed(uint64_t generation,uint64_t revision)override{
        std::lock_guard<std::mutex> lock(mutex_);return !stopped_&&capture_?capture_->retryReviewed(generation,revision):EnergyHistoryAdmission::NotReady;
    }
    EnergyHistoryAdmission authorize(uint64_t generation,uint64_t revision,const EnergyAuthorizationCommand &command)override{
        std::lock_guard<std::mutex> lock(mutex_);
        if(stopped_||!authorizationStarted_)return EnergyHistoryAdmission::NotReady;
        if(command.action==EnergyAuthorizationAction::Provision)return EnergyHistoryAdmission::Invalid;
        if(command.action==EnergyAuthorizationAction::Enroll&&(selected_.empty()||command.devices!=selected_))return EnergyHistoryAdmission::Invalid;
        return authorization_.submit(generation,revision,command);
    }
    EnergyHistoryAdmission acknowledge(uint64_t generation,uint64_t revision)override{
        std::lock_guard<std::mutex> lock(mutex_);return !stopped_&&authorizationStarted_?authorization_.acknowledgeInspection(generation,revision):EnergyHistoryAdmission::NotReady;
    }
    void requestStop()override{
        std::lock_guard<std::mutex> lock(mutex_);attempted_=true;if(stopped_)return;
        stopped_=true;authorization_.requestStop();
        if(capture_)capture_->requestStop();
        // Both owners are revoked before any optional snapshot allocation can fail.
        try{
            if(authorizationStarted_)authorizationView_=authorization_.snapshot();
            else{auto a=*authorizationView_;a.state=EnergyAuthorizationOwnerState::Stopped;a.pending=a.startupPending=false;++a.revision;authorizationView_=std::make_shared<const EnergyAuthorizationSnapshot>(a);}
            if(capture_)captureView_=capture_->snapshot();
            else{auto c=*captureView_;c.state=EnergyCaptureState::Stopped;c.leaseState=EnergyLeaseState::Stopped;++c.revision;captureView_=std::make_shared<const EnergyCaptureSnapshot>(c);}
        }catch(...){}
    }
    void stop()override{
        std::lock_guard<std::mutex> life(lifecycle_);requestStop();
        // Joining is deliberately separate from memory-only revocation; supervisor drains miners first.
        if(capture_)capture_->stop();
        authorization_.stop();
    }
    std::shared_ptr<const EnergyCaptureSnapshot> snapshot()const override{
        std::lock_guard<std::mutex> lock(mutex_);return !stopped_&&capture_?capture_->snapshot():captureView_;
    }
    std::shared_ptr<const EnergyAuthorizationSnapshot> authorizationSnapshot()const override{
        std::lock_guard<std::mutex> lock(mutex_);return !stopped_&&authorizationStarted_?authorization_.snapshot():authorizationView_;
    }
    std::vector<std::string> selectedDevices()const override{std::lock_guard<std::mutex> lock(mutex_);return selected_;}
};
}
std::unique_ptr<EnergyRuntime> makeEnergyRuntime(std::filesystem::path auth,std::filesystem::path collector,std::filesystem::path file,bool mining,bool disabled,unsigned timeout,EnergyHistoryClock clock){
    return std::make_unique<Runtime>(std::move(auth),std::move(collector),std::move(file),mining,disabled,timeout,std::move(clock));
}
} // namespace om
