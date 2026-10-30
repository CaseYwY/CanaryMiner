// Background display-only sampling. No control/safety permission, mining action or automatic retry owner.
#pragma once
#include "../core/platform.h"
#include "../core/telemetry.h"
#include "accepted_history.h"
#include "stats.h"
#include "stats_server.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
namespace om {
struct DisplaySampleTime {int64_t utcMs=0;uint64_t monotonicMs=0;};
inline DisplaySampleTime displaySampleTime(){
    return {std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(),
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())};
}
inline bool displaySampleOrdered(DisplaySampleTime a,DisplaySampleTime b){
    return a.utcMs>0&&b.utcMs<=253402300799999LL&&a.monotonicMs&&a.utcMs<=b.utcMs&&a.monotonicMs<=b.monotonicMs;
}
inline bool displaySampleInterval(DisplaySampleTime a,DisplaySampleTime b,uint64_t maximumMs){
    if(!displaySampleOrdered(a,b))return false;
    const auto wall=static_cast<uint64_t>(b.utcMs-a.utcMs),mono=b.monotonicMs-a.monotonicMs;
    return wall<=maximumMs&&mono<=maximumMs&&(wall>mono?wall-mono:mono-wall)<=100;
}
struct DisplaySampleTarget {std::string uuid;int port=0;};
struct DisplaySampleReading {
    DisplaySampleTime statsObserved,powerBegan,powerFinished;
    std::optional<StatsSnapshot> stats;
    std::optional<uint64_t> accepted;
    GpuTelemetry telemetry;
};
class DisplaySampleReader {
   public:
    virtual ~DisplaySampleReader()=default;
    virtual DisplaySampleReading read(size_t slot,const std::atomic<bool> &cancel)=0;
};
// Construction, reads and destruction all run on the sampling thread. No borrowed supervisor/view references.
using DisplaySampleFactory=std::function<std::unique_ptr<DisplaySampleReader>(const std::vector<DisplaySampleTarget> &)>;
class NativeDisplaySampleReader final:public DisplaySampleReader {
    struct Device {Nvml nvml;bool attempted=false,opened=false;};
    std::vector<DisplaySampleTarget> targets_;
    std::vector<Device> devices_;
   public:
    explicit NativeDisplaySampleReader(std::vector<DisplaySampleTarget> targets):targets_(std::move(targets)),devices_(targets_.size()){}
    ~NativeDisplaySampleReader()override{for(auto &device:devices_)device.nvml.close();}
    DisplaySampleReading read(size_t slot,const std::atomic<bool> &cancel)override{
        DisplaySampleReading out;if(cancel||slot>=targets_.size())return out;
        int status=0;std::string body,error;
        const bool received=httpGetLocal("127.0.0.1",targets_[slot].port,"/api/v1/stats",&status,&body,&error,1);
        out.statsObserved=displaySampleTime();
        if(cancel)return out;
        if(received&&status==200){JsonValue root;StatsSnapshot stats;
            if(parseJsonStrict(body,&root,&error)&&statsFromJson(root,&stats,&error)){
                uint64_t accepted=0;if(acceptedHistoryCounter(root,&accepted))out.accepted=accepted;
                out.stats=std::move(stats);
            }
        }
        auto &device=devices_[slot];out.powerBegan=displaySampleTime();
        if(!cancel&&!device.attempted){device.attempted=true;device.opened=device.nvml.openByUuid(targets_[slot].uuid);}
        if(!cancel&&device.opened)out.telemetry=device.nvml.sample();
        out.powerFinished=displaySampleTime();return out;
    }
};
struct DisplaySampleBatch {
    uint64_t revision=0;
    std::vector<uint64_t> generations; // zero skips an inactive worker; captured before dispatch, never read back later
    std::vector<DisplaySampleReading> readings;
    DisplaySampleTime began,finished;
};
// One fixed background reader, one flight OR one unconsumed result. No queued replacement/catch-up burst.
class SupervisorSampling {
    std::vector<DisplaySampleTarget> targets_;DisplaySampleFactory factory_;
    mutable std::mutex mutex_;std::condition_variable ready_;std::thread thread_;
    std::atomic<bool> cancel_{true};bool attempted_=false,outstanding_=false,faulted_=false;
    uint64_t revision_=0;
    std::unique_ptr<DisplaySampleBatch> request_;
    std::shared_ptr<const DisplaySampleBatch> result_;
    void run()noexcept{
        try{
            std::unique_ptr<DisplaySampleReader> reader;
            for(;;){std::unique_ptr<DisplaySampleBatch> job;
                {std::unique_lock<std::mutex> lock(mutex_);ready_.wait(lock,[&]{return cancel_.load()||request_!=nullptr;});
                    if(cancel_)return;
                    job=std::move(request_);}
                if(cancel_)return;
                if(!reader)reader=factory_?factory_(targets_):std::make_unique<NativeDisplaySampleReader>(targets_);
                if(!reader)throw 1;
                job->began=displaySampleTime();job->readings.resize(targets_.size());
                for(size_t i=0;i<targets_.size()&&!cancel_;++i)if(job->generations[i])job->readings[i]=reader->read(i,cancel_);
                job->finished=displaySampleTime();
                if(!displaySampleOrdered(job->began,job->finished))throw 1;
                {std::lock_guard<std::mutex> lock(mutex_);if(cancel_)return;result_=std::move(job);}
            }
        }catch(...){std::lock_guard<std::mutex> lock(mutex_);if(!cancel_)faulted_=true;
            cancel_=true;outstanding_=false;request_.reset();result_.reset();}
    }
   public:
    SupervisorSampling()=default;
    ~SupervisorSampling(){stop();}
    SupervisorSampling(const SupervisorSampling &)=delete;
    SupervisorSampling &operator=(const SupervisorSampling &)=delete;
    bool start(std::vector<DisplaySampleTarget> targets,DisplaySampleFactory factory={}){
        std::lock_guard<std::mutex> lock(mutex_);if(attempted_)return false;attempted_=true;
        if(targets.empty()||targets.size()>256)return false;
        for(const auto &target:targets)if(target.uuid.empty()||target.uuid.size()>128||target.uuid.find('\0')!=target.uuid.npos||target.port<1||target.port>65535)return false;
        targets_=std::move(targets);factory_=std::move(factory);cancel_=false;
        try{thread_=std::thread([this]{run();});return true;}catch(...){cancel_=true;faulted_=true;return false;}
    }
    bool request(const std::vector<uint64_t> &generations){
        std::lock_guard<std::mutex> lock(mutex_);
        if(cancel_||outstanding_||generations.size()!=targets_.size()||revision_==UINT64_MAX||
            std::none_of(generations.begin(),generations.end(),[](uint64_t value){return value!=0;}))return false;
        try{auto job=std::make_unique<DisplaySampleBatch>();job->revision=++revision_;job->generations=generations;
            request_=std::move(job);outstanding_=true;ready_.notify_one();return true;
        }catch(...){faulted_=true;cancel_=true;ready_.notify_one();return false;}
    }
    std::shared_ptr<const DisplaySampleBatch> take(){
        std::lock_guard<std::mutex> lock(mutex_);if(!result_||cancel_)return {};
        auto result=std::move(result_);outstanding_=false;return result;
    }
    bool pending()const{std::lock_guard<std::mutex> lock(mutex_);return outstanding_;}
    bool faulted()const{std::lock_guard<std::mutex> lock(mutex_);return faulted_;}
    void requestStop(){
        std::lock_guard<std::mutex> lock(mutex_);attempted_=true;cancel_=true;outstanding_=false;request_.reset();result_.reset();ready_.notify_one();
    }
    void stop(){requestStop();if(thread_.joinable())thread_.join();} // only after worker drains; never detach a native call
};
} // namespace om
