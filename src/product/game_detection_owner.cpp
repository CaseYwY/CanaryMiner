#include "game_detection_owner.h"
#include "native_process_admission.h"
#include <condition_variable>
#include <mutex>
#include <thread>
namespace om {
namespace {using S=GameDetectionOwnerState;}
struct GameDetectionOwner::Impl {
    GameProbeClient client;std::filesystem::path helper;GameProcessTarget target;GameDetectionPolicy policy;GameDetectionClock clock;
    NativeProcessLifetime nativeLife;bool nativeFaulted=false;
    mutable std::mutex mutex;std::mutex lifecycle;std::condition_variable wake;std::thread thread;std::atomic<bool> cancel{false};
    bool attempted=false,stopped=true;uint64_t generation=0,revision=0,next=0;std::string scope;
    GameGuardTime anchor,last;GameDetectionSnapshot current;
    std::shared_ptr<const GameDetectionSnapshot> published=std::make_shared<const GameDetectionSnapshot>();
    const std::shared_ptr<const GameDetectionSnapshot> empty=published;
    Impl(std::filesystem::path h,GameProcessTarget t,GameDetectionPolicy p,GameDetectionClock c):client(h),helper(std::move(h)),target(std::move(t)),policy(p),clock(c?std::move(c):GameDetectionClock(gameDetectionTime)){}
    void publish(){published=std::make_shared<const GameDetectionSnapshot>(current);}
    void halt(S state){stopped=true;cancel=true;current.state=state;current.pending=false;current.detection.state=GamePresence::Unknown;current.detection.episodeKey.clear();wake.notify_all();}
    bool nativeCurrent()noexcept{ // mutex held; old observations never become current authority after a foreign fault
        if(!nativeLife.bound()||nativeLife.current())return true;
        if(!nativeFaulted){nativeFaulted=true;current.transport=LocalTaskStatus::Unreaped;current.status=GameProbeStatus::Incomplete;
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
        try{for(;;){GameProbeRequest r;
                {std::unique_lock<std::mutex> lock(mutex);if(!nativeCurrent()||stopped)return;const auto now=clock();
                    if(!time(now)){publish();return;}
                    if(now.monotonicMs<next){wake.wait_for(lock,std::chrono::milliseconds(25));continue;}
                    if(revision==UINT64_MAX){halt(S::Faulted);publish();return;}
                    r.enabled=true;r.generation=generation;r.revision=++revision;r.target=target;r.privateScope=scope;r.issued=now;r.scanBudgetMs=policy.scanBudgetMs;r.maxProcesses=policy.maxProcesses;
                    current.pending=true;publish();
                }
                const auto result=client.transact(r,cancel,policy.helperTimeoutMs);
                std::lock_guard<std::mutex> lock(mutex);
                if(result.transport==LocalTaskStatus::Unreaped){current.unreaped=true;current.transport=result.transport;if(!stopped)halt(S::Faulted);nativeCurrent();publish();return;}
                if(!nativeCurrent()||stopped)return;
                const auto now=clock();if(!time(now)){publish();return;}
                current.pending=false;++current.completed;current.transport=result.transport;current.state=S::Waiting;current.status=GameProbeStatus::Incomplete;
                current.detection={GamePresence::Unknown,target.gameKey,"",generation,r.revision,r.issued};
                if(result.transport==LocalTaskStatus::Ok&&result.reply){
                    const auto &p=*result.reply;scope=p.privateScope;current.status=p.status;current.detection=gameProbeDetection(r,p);
                    if(p.status==GameProbeStatus::Ok)current.state=S::Ready;
                    if(p.status==GameProbeStatus::Clock)halt(S::ClockFault);
                }
                if(now.monotonicMs>UINT64_MAX-policy.pollMs)halt(S::ClockFault);else next=now.monotonicMs+policy.pollMs; // no catch-up or overlapping retries
                publish();if(stopped)return;
            }
        }catch(...){try{std::lock_guard<std::mutex> lock(mutex);if(!stopped)halt(S::Faulted);publish();}catch(...){} }
    }
};
GameDetectionOwner::GameDetectionOwner(std::filesystem::path helper,GameProcessTarget target,GameDetectionPolicy policy,GameDetectionClock clock)
    :impl_(new Impl(std::move(helper),std::move(target),policy,std::move(clock))){}
GameDetectionOwner::~GameDetectionOwner(){stop();}
bool GameDetectionOwner::start(uint64_t generation){
    auto &s=*impl_;std::lock_guard<std::mutex> life(s.lifecycle);std::lock_guard<std::mutex> lock(s.mutex);if(s.attempted)return false;s.attempted=true;
    try{
        if(!s.policy.enabled){s.current.state=S::Disabled;s.publish();return false;}
        if(!s.nativeLife.begin()){s.nativeCurrent();return false;}
        s.anchor=s.last=s.clock();if(!s.nativeCurrent()||!s.nativeLife.permitted())return false;
        GameProbeRequest r;r.generation=generation;r.revision=1;r.target=s.target;r.issued=s.last;r.scanBudgetMs=s.policy.scanBudgetMs;r.maxProcesses=s.policy.maxProcesses;
        if(!validGameProbeRequest(r)||!localTaskPath(s.helper)||s.policy.pollMs<100||s.policy.pollMs>10000||s.policy.helperTimeoutMs<50||s.policy.helperTimeoutMs>2000||s.policy.helperTimeoutMs<s.policy.scanBudgetMs){s.halt(S::Faulted);s.publish();return false;}
        s.generation=generation;s.stopped=false;s.current.state=S::Waiting;s.current.detection.gameKey=s.target.gameKey;s.current.detection.generation=generation;s.publish();
        s.thread=std::thread([p=&s]{p->run();});return true;
    }catch(...){s.halt(S::Faulted);try{s.publish();}catch(...){}return false;}
}
void GameDetectionOwner::requestStop(){auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);s.attempted=true;s.halt(S::Stopped);try{s.publish();}catch(...){} }
void GameDetectionOwner::stop(){auto &s=*impl_;std::lock_guard<std::mutex> life(s.lifecycle);requestStop();if(s.thread.joinable())s.thread.join();}
std::shared_ptr<const GameDetectionSnapshot> GameDetectionOwner::snapshot()const{
    auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);s.nativeCurrent();
    if(s.nativeLife.current()&&!s.nativeLife.permitted())return s.empty;
    return s.published->state!=s.current.state||s.published->transport!=s.current.transport||s.published->unreaped!=s.current.unreaped?s.empty:s.published;
}
} // namespace om
