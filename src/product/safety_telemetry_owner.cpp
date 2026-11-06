#include "safety_telemetry_owner.h"
#include "native_process_admission.h"
#include <condition_variable>
#include <mutex>
#include <thread>
namespace om {
#if defined(CANARY_SAFETY_TELEMETRY_OWNER_TESTING)
SafetyTelemetryResult safetyTelemetryOwnerTestSample(const SafetyTelemetryRequest &,const std::atomic<bool> &,unsigned);
#endif
namespace {using S=SafetyTelemetryOwnerState;}
struct SafetyTelemetryOwner::Impl {
    SafetyTelemetryClient client;std::filesystem::path helper;std::vector<std::string> devices;SafetyTelemetryPolicy policy;SafetyTelemetryClock clock;
    NativeProcessLifetime nativeLife;bool nativeFaulted=false;
    mutable std::mutex mutex;std::mutex lifecycle;std::condition_variable wake;std::thread thread;std::atomic<bool> cancel{false};
    bool attempted=false,stopped=true;uint64_t epoch=0,revision=0,next=0;GameGuardTime anchor,last;
    SafetyTelemetrySnapshot current;std::shared_ptr<const SafetyTelemetrySnapshot> published=std::make_shared<const SafetyTelemetrySnapshot>();
    const std::shared_ptr<const SafetyTelemetrySnapshot> empty=published;
    Impl(std::filesystem::path h,std::vector<std::string> d,SafetyTelemetryPolicy p,SafetyTelemetryClock c)
        :client(h),helper(std::move(h)),devices(std::move(d)),policy(p),clock(c?std::move(c):SafetyTelemetryClock(safetyTelemetryTime)){}
    void publish(){published=std::make_shared<const SafetyTelemetrySnapshot>(current);}
    void halt(S state){stopped=true;cancel=true;current.state=state;current.pending=false;current.reply.reset();wake.notify_all();}
    bool nativeCurrent()noexcept{ // mutex held; no source callback, native call or join
        if(!nativeLife.bound()||nativeLife.current())return true;
        if(!nativeFaulted){nativeFaulted=true;current.transport=LocalTaskStatus::Unreaped;current.status=SafetyTelemetryStatus::Unavailable;
            halt(current.state==S::Stopped?S::Stopped:S::Faulted);try{publish();}catch(...){} }
        return false;
    }
    bool time(GameGuardTime now){
        if(!nativeCurrent()||!nativeLife.permitted())return false;
        if(now.utcMs<=0||now.utcMs>253402300799999LL||now.utcMs<last.utcMs||now.monotonicMs<last.monotonicMs){halt(S::ClockFault);return false;}
        const auto wall=static_cast<uint64_t>(now.utcMs-anchor.utcMs),mono=now.monotonicMs-anchor.monotonicMs;
        if((wall>mono?wall-mono:mono-wall)>2000){halt(S::ClockFault);return false;}last=now;return true;
    }
    void run()noexcept{
        NativeProcessScope original(nativeLife.epoch());
        try{for(;;){SafetyTelemetryRequest request;
            {std::unique_lock<std::mutex> lock(mutex);if(!nativeCurrent()||stopped)return;const auto now=clock();if(!time(now)){publish();return;}
                if(now.monotonicMs<next){wake.wait_for(lock,std::chrono::milliseconds(25));continue;}
                if(revision==UINT64_MAX){halt(S::Faulted);publish();return;}
                request={true,epoch,++revision,now,policy.collectionBudgetMs,devices,policy.profile};current.pending=true;publish();}
#if defined(CANARY_SAFETY_TELEMETRY_OWNER_TESTING)
            const auto result=safetyTelemetryOwnerTestSample(request,cancel,policy.helperTimeoutMs);
#else
            const auto result=client.sample(request,cancel,policy.helperTimeoutMs);
#endif
            std::lock_guard<std::mutex> lock(mutex);
            if(result.transport==LocalTaskStatus::Unreaped){current.unreaped=true;current.transport=result.transport;current.request=request;
                current.status=SafetyTelemetryStatus::Unavailable;halt(S::Faulted);nativeCurrent();publish();return;}
            if(!nativeCurrent()||stopped)return;
            const auto now=clock();if(!time(now)){publish();return;}
            current.pending=false;++current.completed;current.transport=result.transport;current.request=request;current.reply=result.reply;current.state=S::Waiting;
            current.status=result.reply?result.reply->status:SafetyTelemetryStatus::Unavailable;
            if(result.reply){const auto &reply=*result.reply;
                if(reply.began.utcMs<request.issued.utcMs||reply.began.monotonicMs<request.issued.monotonicMs||
                    reply.finished.utcMs<reply.began.utcMs||reply.finished.monotonicMs<reply.began.monotonicMs||
                    reply.began.utcMs>now.utcMs||reply.began.monotonicMs>now.monotonicMs||reply.finished.utcMs>now.utcMs||reply.finished.monotonicMs>now.monotonicMs||reply.status==SafetyTelemetryStatus::Clock)halt(S::ClockFault);
                else if(reply.status==SafetyTelemetryStatus::DeviceFault||reply.status==SafetyTelemetryStatus::Identity||reply.status==SafetyTelemetryStatus::Invalid)halt(S::Faulted);
                else if(reply.status==SafetyTelemetryStatus::Ok)current.state=S::Ready;
            }
            if(now.monotonicMs>UINT64_MAX-policy.pollMs)halt(S::ClockFault);else next=now.monotonicMs+policy.pollMs;
            publish();if(stopped)return;
        }}catch(...){try{std::lock_guard<std::mutex> lock(mutex);if(!stopped)halt(S::Faulted);publish();}catch(...){} }
    }
};
SafetyTelemetryOwner::SafetyTelemetryOwner(std::filesystem::path h,std::vector<std::string> d,SafetyTelemetryPolicy p,SafetyTelemetryClock c)
    :impl_(new Impl(std::move(h),std::move(d),p,std::move(c))){}
SafetyTelemetryOwner::~SafetyTelemetryOwner(){stop();}
bool SafetyTelemetryOwner::start(uint64_t epoch){
    auto &p=*impl_;std::lock_guard<std::mutex> life(p.lifecycle);std::lock_guard<std::mutex> lock(p.mutex);if(p.attempted)return false;p.attempted=true;
    try{if(!p.policy.enabled){p.current.state=S::Disabled;p.publish();return false;}
        if(!p.nativeLife.begin()){p.nativeCurrent();return false;}
        p.anchor=p.last=p.clock();if(!p.nativeCurrent()||!p.nativeLife.permitted())return false;
        SafetyTelemetryRequest r{true,epoch,1,p.last,p.policy.collectionBudgetMs,p.devices,p.policy.profile};
        if(!validSafetyTelemetryRequest(r)||!localTaskPath(p.helper)||p.policy.pollMs<100||p.policy.pollMs>10000||p.policy.helperTimeoutMs<50||
            p.policy.helperTimeoutMs>2000||p.policy.helperTimeoutMs<p.policy.collectionBudgetMs){p.halt(S::Faulted);p.publish();return false;}
        p.epoch=epoch;p.stopped=false;p.current.state=S::Waiting;p.publish();p.thread=std::thread([&p]{p.run();});return true;
    }catch(...){p.halt(S::Faulted);try{p.publish();}catch(...){}return false;}
}
void SafetyTelemetryOwner::requestStop(){auto &p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);p.attempted=true;p.halt(p.current.unreaped?S::Faulted:S::Stopped);try{p.publish();}catch(...){} }
void SafetyTelemetryOwner::stop(){auto &p=*impl_;std::lock_guard<std::mutex> life(p.lifecycle);requestStop();if(p.thread.joinable())p.thread.join();}
std::shared_ptr<const SafetyTelemetrySnapshot> SafetyTelemetryOwner::snapshot()const{
    auto &p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);p.nativeCurrent();
    if(p.nativeLife.current()&&!p.nativeLife.permitted())return p.empty;
    return p.published->state!=p.current.state||p.published->transport!=p.current.transport||p.published->unreaped!=p.current.unreaped?p.empty:p.published;
}
} // namespace om
