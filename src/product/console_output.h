// Creation-bound CPU display owner. The foreground only queues bytes and revokes authority.
#pragma once
#include "console_output_protocol.h"
#include "duplex_task_process.h"
#include "session_intent.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
namespace om {
enum class ConsoleOutputState { Dormant, Starting, Ready, Faulted, Stopped };
enum class ConsoleCleanup { None, Cursor, IdleClosed, IdleRefused, IdleTerminalFault, SupervisorStopped };
struct ConsoleOutputPolicy {unsigned operationTimeoutMs=2000,reapMs=250,cleanupMs=200;bool terminalInput=false;};
struct ConsoleOutputSnapshot {
    ConsoleOutputState state=ConsoleOutputState::Dormant;
    LocalTaskStatus transport=LocalTaskStatus::Unavailable;
    uint64_t issued=0,accepted=0;
    std::size_t queued=0,queuedBytes=0;
    bool inFlight=false,settled=false,outputAccepted=false;
    unsigned columns=80,rows=24;
};
class ConsoleOutput {
    using Clock=std::chrono::steady_clock;
    struct Job {std::string bytes;Clock::time_point admitted;bool screen=false;};
    std::filesystem::path helper_;
    const SessionIntent &intent_;SessionToken token_;ConsoleOutputPolicy policy_;
    NativeProcessLifetime life_;mutable std::mutex mutex_;std::mutex lifecycle_;
    std::condition_variable wake_;std::atomic<bool> cancel_{true};std::thread thread_;
    bool attempted_=false,shutdown_=false;Clock::time_point started_;
    ConsoleCleanup cleanup_=ConsoleCleanup::None;
    uint64_t finalWorkers_=0,finalSeconds_=0;bool finalDashboard_=false,finalFailure_=false;
    ConsoleOutputSnapshot view_;std::deque<Job> queue_;std::deque<int> keys_;
    bool foreign()const{return intent_.foreignCaller(token_)||(life_.bound()&&life_.current()&&!life_.permitted());}
    void revoke(ConsoleOutputState state,LocalTaskStatus status){
        cancel_=true;queue_.clear();keys_.clear();view_.queued=view_.queuedBytes=0;
        // A later Stop cannot turn a known output failure into successful cancellation.
        if(view_.state!=ConsoleOutputState::Faulted){view_.state=state;view_.transport=status;}
        wake_.notify_all();
    }
    void close(ConsoleCleanup cleanup){
        shutdown_=true;if(cleanup_==ConsoleCleanup::None)cleanup_=cleanup;
        revoke(ConsoleOutputState::Stopped,LocalTaskStatus::Cancelled);
    }
    bool current(){
        if(view_.state==ConsoleOutputState::Dormant||view_.state==ConsoleOutputState::Faulted||view_.state==ConsoleOutputState::Stopped)return false;
        if(!life_.current()){revoke(ConsoleOutputState::Faulted,LocalTaskStatus::Unreaped);return false;}
        if(!intent_.permits(token_)){revoke(ConsoleOutputState::Stopped,LocalTaskStatus::Cancelled);return false;}
        return !cancel_.load();
    }
    LocalTaskStatus exchange(DuplexTaskProcess &process,uint64_t ticket,const Job &job,const std::atomic<bool> &cancel,unsigned timeout,bool terminal=false){
        std::string wire;if(!encodeConsoleOutput(ticket,job.bytes,&wire))return LocalTaskStatus::Invalid;
        const auto valid=[&]{const auto age=Clock::now()-job.admitted;return age>=Clock::duration::zero()&&age<std::chrono::milliseconds(timeout);};
        if(cancel.load()||!life_.current())return life_.current()?LocalTaskStatus::Cancelled:LocalTaskStatus::Unreaped;
        if(!valid())return LocalTaskStatus::Timeout;
        if(!process.send(wire))return LocalTaskStatus::Unavailable;
        std::string ack;bool flushed=false;
        for(;;){
            if(cancel.load()||!life_.current())return life_.current()?LocalTaskStatus::Cancelled:LocalTaskStatus::Unreaped;
            if(!valid())return LocalTaskStatus::Timeout;
            const auto result=process.poll();flushed=flushed||result.flushed;
            if(result.state!=DuplexTaskState::Running||result.channelFault||result.eof)return result.state==DuplexTaskState::Unreaped?LocalTaskStatus::Unreaped:LocalTaskStatus::Unavailable;
            if(result.bytes.size()>consoleOutputHeaderLimit-ack.size())return LocalTaskStatus::Invalid;
            ack+=result.bytes;
            if(ack.find('\n')!=ack.npos){
                uint64_t t=0;std::size_t size=0;unsigned columns=80,rows=24;int key=0;
                const bool decoded=terminal?decodeConsoleTerminalAcknowledgement(ack,&t,&size,&columns,&rows,&key):console_output_detail::header(ack,"CMTA1 ",&t,&size);
                if(!flushed||!decoded||t!=ticket||size!=job.bytes.size())return LocalTaskStatus::Invalid;
                if(!valid())return LocalTaskStatus::Timeout;
                if(cancel.load()||!life_.current())return life_.current()?LocalTaskStatus::Cancelled:LocalTaskStatus::Unreaped;
                if(terminal){std::lock_guard<std::mutex> lock(mutex_);
                    if(cancel.load()||!life_.current())return life_.current()?LocalTaskStatus::Cancelled:LocalTaskStatus::Unreaped;
                    if(key&&keys_.size()>=64)return LocalTaskStatus::Invalid;
                    view_.columns=columns;view_.rows=rows;if(key)keys_.push_back(key);
                }
                return LocalTaskStatus::Ok; // accepted by the native output operation, not proof a human saw it
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    std::string cleanupBytes(ConsoleCleanup cleanup)const{
        switch(cleanup){
            case ConsoleCleanup::Cursor:return "\x1b[?25h";
            case ConsoleCleanup::IdleClosed:return "\x1b[2J\x1b[H\x1b[?25hSettings closed. No mining was started.\n";
            case ConsoleCleanup::IdleRefused:return "\x1b[2J\x1b[H\x1b[?25hIdle preference inspection refused; no synchronous fallback or automatic retry. An issued write may be UNKNOWN.\n";
            case ConsoleCleanup::IdleTerminalFault:return "\x1b[2J\x1b[H\x1b[?25hIdle terminal I/O unavailable; no synchronous fallback or automatic retry. An issued preference write may be UNKNOWN. No mining was started.\n";
            case ConsoleCleanup::SupervisorStopped:return std::string(finalDashboard_?"\x1b[2J\x1b[H\x1b[?25h":"")+
                "stopped: "+std::to_string(finalWorkers_)+" GPU(s), session "+std::to_string(finalSeconds_)+" s"+
                (finalFailure_?"; session not clean; inspect exit status before a new explicit start\n":"\n");
            default:return "";
        }
    }
    bool cleanup(ConsoleCleanup reason,DuplexTaskProcess *prepared){
        if(reason==ConsoleCleanup::None||!life_.current())return true;
        if(prepared){
            std::atomic<bool> cancel{false};const auto admitted=Clock::now();
            (void)exchange(*prepared,2,Job{cleanupBytes(reason),admitted,false},cancel,policy_.cleanupMs);
            return prepared->terminate(policy_.reapMs);
        }
#if defined(_WIN32)
        return true; // never duplicate/query/attach console handles after output may already be blocked
#else
        // A separate, once-only fixed cleanup after exit proof; never replay the private queue.
        DuplexTaskProcess process;std::atomic<bool> cancel{false};const auto admitted=Clock::now();
        bool delivered=process.start(helper_,cancel,DuplexTaskOutput::InheritedStdout)&&
            exchange(process,1,Job{{},admitted,false},cancel,policy_.cleanupMs)==LocalTaskStatus::Ok;
        if(delivered)delivered=exchange(process,2,Job{cleanupBytes(reason),admitted,false},cancel,policy_.cleanupMs)==LocalTaskStatus::Ok;
        (void)delivered; // cleanup is explicitly best effort; failure cannot revive output or mining authority
        return process.terminate(policy_.reapMs);
#endif
    }
    void run()noexcept{
        NativeProcessScope original(life_.epoch());bool settled=false,started=false,finalReady=false;
        std::unique_ptr<DuplexTaskProcess> finalProcess;
        try{
            DuplexTaskProcess process;
            started=process.start(helper_,cancel_,policy_.terminalInput?DuplexTaskOutput::InheritedTerminal:DuplexTaskOutput::InheritedStdout);
            auto status=started?LocalTaskStatus::Ok:LocalTaskStatus::Unavailable;
#if defined(_WIN32)
            if(started){
                // GetConsoleMode/handle attachment can block behind ConPTY output too. Prepare the
                // one-shot, output-only finalizer before the original helper receives any display bytes.
                finalProcess=std::make_unique<DuplexTaskProcess>();
                status=finalProcess->start(helper_,cancel_,DuplexTaskOutput::InheritedStdout)?
                    exchange(*finalProcess,1,Job{{},started_,false},cancel_,policy_.operationTimeoutMs):LocalTaskStatus::Unavailable;
                finalReady=status==LocalTaskStatus::Ok;
            }
#endif
            if(status==LocalTaskStatus::Ok)status=exchange(process,1,Job{{},started_,false},cancel_,policy_.operationTimeoutMs,policy_.terminalInput);
            {std::lock_guard<std::mutex> lock(mutex_);
                if(status!=LocalTaskStatus::Ok){if(!cancel_.load())revoke(ConsoleOutputState::Faulted,status);}
                else if(!cancel_.load()&&life_.current()){view_.state=ConsoleOutputState::Ready;view_.transport=status;view_.issued=view_.accepted=1;}}
            while(status==LocalTaskStatus::Ok){
                Job job;uint64_t ticket=0;bool have=false;
                {std::unique_lock<std::mutex> lock(mutex_);
                    wake_.wait_for(lock,std::chrono::milliseconds(5),[&]{return cancel_.load()||!queue_.empty();});
                    if(!life_.current()){revoke(ConsoleOutputState::Faulted,LocalTaskStatus::Unreaped);break;}
                    if(cancel_.load())break;
                    if(!queue_.empty()||policy_.terminalInput){
                        if(view_.issued==UINT64_MAX){revoke(ConsoleOutputState::Faulted,LocalTaskStatus::Invalid);break;}
                        if(!queue_.empty()){job=std::move(queue_.front());queue_.pop_front();view_.queuedBytes-=job.bytes.size();view_.queued=queue_.size();}
                        else job=Job{{},Clock::now(),false}; // bounded native key/geometry poll, no foreground console API
                        view_.inFlight=true;ticket=++view_.issued;have=true;
                    }
                }
                if(!have){
                    const auto idle=process.poll();
                    if(idle.state!=DuplexTaskState::Running||idle.channelFault||idle.eof||!idle.bytes.empty()){
                        std::lock_guard<std::mutex> lock(mutex_);revoke(ConsoleOutputState::Faulted,LocalTaskStatus::Unavailable);break;
                    }
                    continue;
                }
                status=exchange(process,ticket,job,cancel_,policy_.operationTimeoutMs,policy_.terminalInput);
                {std::lock_guard<std::mutex> lock(mutex_);
                    view_.inFlight=false;
                    if(status!=LocalTaskStatus::Ok){if(!cancel_.load())revoke(ConsoleOutputState::Faulted,status);break;}
                    if(!cancel_.load()&&life_.current()){view_.accepted=ticket;view_.outputAccepted=view_.outputAccepted||!job.bytes.empty();}
                }
            }
            settled=process.terminate(policy_.reapMs);
        }catch(...){std::lock_guard<std::mutex> lock(mutex_);settled=life_.current();revoke(ConsoleOutputState::Faulted,settled?LocalTaskStatus::Unavailable:LocalTaskStatus::Unreaped);}
        ConsoleCleanup reason=ConsoleCleanup::None;
        {std::unique_lock<std::mutex> lock(mutex_);view_.settled=settled&&!finalProcess;view_.inFlight=false;
            if(!settled)revoke(ConsoleOutputState::Faulted,LocalTaskStatus::Unreaped);
            // On failure retain the latch until the foreground stops. Never restart a display flight.
            wake_.wait(lock,[&]{return shutdown_;});reason=cleanup_;
        }
        if(settled&&started&&life_.current()){
            try{settled=cleanup(reason,finalReady?finalProcess.get():nullptr);}catch(...){settled=life_.current();} // RAII has settled or quarantined any created helper
        }
        if(finalProcess){const bool finalSettled=finalProcess->terminate(policy_.reapMs);settled=settled&&finalSettled;}
        std::lock_guard<std::mutex> lock(mutex_);view_.settled=settled;
        if(!settled){view_.state=ConsoleOutputState::Faulted;view_.transport=LocalTaskStatus::Unreaped;}
    }
  public:
    ConsoleOutput(std::filesystem::path helper,const SessionIntent &intent,SessionToken token,ConsoleOutputPolicy policy={}):helper_(std::move(helper)),intent_(intent),token_(token),policy_(policy){}
    ConsoleOutput(const ConsoleOutput &)=delete;ConsoleOutput &operator=(const ConsoleOutput &)=delete;
    ~ConsoleOutput(){std::lock_guard<std::mutex> lifecycle(lifecycle_);{std::lock_guard<std::mutex> lock(mutex_);close(ConsoleCleanup::None);}if(thread_.joinable())thread_.join();}
    bool start(){
        std::lock_guard<std::mutex> lifecycle(lifecycle_);std::lock_guard<std::mutex> lock(mutex_);
        if(foreign()||attempted_||shutdown_||view_.state!=ConsoleOutputState::Dormant||!intent_.permits(token_)||policy_.operationTimeoutMs<50||policy_.operationTimeoutMs>60000||!policy_.reapMs||policy_.reapMs>1000||!policy_.cleanupMs||policy_.cleanupMs>500||!localTaskPath(helper_)||!life_.begin())return false;
        attempted_=true;cancel_=false;started_=Clock::now();view_.state=ConsoleOutputState::Starting;
        try{thread_=std::thread([this]{run();});return true;}catch(...){view_.settled=true;revoke(ConsoleOutputState::Faulted,LocalTaskStatus::Unavailable);return false;}
    }
    bool submit(std::string bytes,bool screen=false){
        std::lock_guard<std::mutex> lock(mutex_);
        if(foreign()||!current())return false;
        if(bytes.empty()||bytes.size()>consoleOutputLimit){revoke(ConsoleOutputState::Faulted,LocalTaskStatus::Invalid);return false;}
        try{
            const auto began=Clock::now();const bool replace=screen&&!queue_.empty()&&queue_.back().screen;
            const auto old=replace?queue_.back().bytes.size():0;
            if((!replace&&queue_.size()>=32)||view_.queuedBytes-old+bytes.size()>1048576){revoke(ConsoleOutputState::Faulted,LocalTaskStatus::Invalid);return false;}
            if(replace){queue_.back()=Job{std::move(bytes),began,true};view_.queuedBytes-=old;view_.queuedBytes+=queue_.back().bytes.size();}
            else{queue_.push_back(Job{std::move(bytes),began,screen});view_.queuedBytes+=queue_.back().bytes.size();}
            view_.queued=queue_.size();wake_.notify_one();return true;
        }catch(...){revoke(ConsoleOutputState::Faulted,LocalTaskStatus::Unavailable);return false;}
    }
    ConsoleOutputSnapshot snapshot(){
        std::lock_guard<std::mutex> lock(mutex_);if(foreign()){ConsoleOutputSnapshot denied;denied.state=ConsoleOutputState::Stopped;denied.transport=LocalTaskStatus::Cancelled;return denied;}current();return view_;
    }
    int pollKey(){std::lock_guard<std::mutex> lock(mutex_);if(foreign()||!current()||keys_.empty())return 0;const int key=keys_.front();keys_.pop_front();return key;}
    void requestStop(ConsoleCleanup cleanup=ConsoleCleanup::None){std::lock_guard<std::mutex> lock(mutex_);if(!foreign()){if(cleanup_==ConsoleCleanup::None)cleanup_=cleanup;revoke(ConsoleOutputState::Stopped,LocalTaskStatus::Cancelled);}}
    void stop(){std::lock_guard<std::mutex> lifecycle(lifecycle_);{std::lock_guard<std::mutex> lock(mutex_);if(foreign())return;close(ConsoleCleanup::None);}if(thread_.joinable())thread_.join();}
    // Only typed non-private counts can cross Stop; no old frame/log or arbitrary footer is replayed.
    void stopSupervisor(uint64_t workers,uint64_t seconds,bool dashboard,bool failed=false){
        std::lock_guard<std::mutex> lifecycle(lifecycle_);
        {std::lock_guard<std::mutex> lock(mutex_);if(foreign())return;finalWorkers_=workers;finalSeconds_=seconds;finalDashboard_=dashboard;finalFailure_=failed||view_.state==ConsoleOutputState::Faulted;close(ConsoleCleanup::SupervisorStopped);}
        if(thread_.joinable())thread_.join();
    }
};
} // namespace om
