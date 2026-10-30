#include "worker_process_owner.h"
#include <chrono>
#include <thread>
namespace om {
struct WorkerProcessOwner::Impl {
    mutable std::mutex mutex;std::atomic<bool> cancel{false};std::thread thread;
    bool attempted=false;WorkerOwnerSnapshot view;WorkerControlBinding binding;
    std::optional<WorkerCommand> pending;
    std::deque<std::string> lines;size_t bytes=0;
    std::string fragment;bool dropping=false;
    void append(const std::string &text){
        for(char c:text){
            if(c=='\n'){
                if(dropping){dropping=false;++view.droppedLines;}
                else if(!fragment.empty()){
                    if(fragment.back()=='\r')fragment.pop_back();
                    if(lines.size()==32||bytes+fragment.size()>32768)++view.droppedLines;
                    else{bytes+=fragment.size();lines.push_back(std::move(fragment));}}
                fragment.clear();
            }else if(!dropping){if(fragment.size()==8192){dropping=true;fragment.clear();}else fragment+=c;}
        }
    }
    void complete(WorkerOwnerState state,int code,bool reaped){
        std::lock_guard<std::mutex> lock(mutex);view.state=state;view.exitCode=code;
        view.reaped=reaped;view.revoked=true;view.done=true;
    }
    void run(std::filesystem::path exe,std::vector<std::string> args,WorkerCommand initial,unsigned drain){
        WorkerControlProcess process;bool fault=false,stopping=false;
        auto deadline=std::chrono::steady_clock::time_point::max();
        try{
            if(!process.start(exe,args,initial,cancel)){
                const auto p=process.poll();const bool unresolved=p.state==WorkerProcessState::Unreaped,stopped=cancel.load();
                complete(unresolved?WorkerOwnerState::Unreaped:stopped?WorkerOwnerState::Exited:WorkerOwnerState::Faulted,
                    unresolved||!stopped?8:process.reaped()&&p.exitCode!=0?7:0,process.reaped());return;
            }
            {std::lock_guard<std::mutex> lock(mutex);if(!cancel.load())view.state=WorkerOwnerState::Running;}
            for(;;){
                if(cancel.load()&&!stopping){process.requestStop();stopping=true;deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(drain);}
                if(!stopping){std::optional<WorkerCommand> next;
                    {std::lock_guard<std::mutex> lock(mutex);if(!cancel.load())next=pending;}
                    if(next&&!cancel.load()){
                        const bool accepted=process.submit(*next);
                        {std::lock_guard<std::mutex> lock(mutex);
                            if(accepted)pending.reset();else{fault=true;view.state=WorkerOwnerState::Faulted;view.revoked=true;}}
                        if(!accepted){process.requestStop();stopping=true;deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(drain);}
                    }}
                if(cancel.load()&&!stopping){process.requestStop();stopping=true;deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(drain);}
                const auto p=process.poll();
                {std::lock_guard<std::mutex> lock(mutex);append(p.output);
                    if(!cancel.load()&&!fault&&p.state==WorkerProcessState::Running)for(const auto &receipt:p.receipts)view.receipt=receipt;}
                if(p.state==WorkerProcessState::Exited){
                    complete(fault||p.exitCode==8?WorkerOwnerState::Faulted:WorkerOwnerState::Exited,fault?8:p.exitCode,process.reaped());return;
                }
                if(p.state==WorkerProcessState::Unreaped){fault=true;break;}
                if(p.state==WorkerProcessState::Failed&&!stopping){fault=true;process.requestStop();stopping=true;deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(drain);
                    std::lock_guard<std::mutex> lock(mutex);view.state=WorkerOwnerState::Faulted;view.revoked=true;}
                if(stopping&&std::chrono::steady_clock::now()>=deadline)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }catch(...){fault=true;}
        {std::lock_guard<std::mutex> lock(mutex);view.revoked=true;view.state=fault?WorkerOwnerState::Faulted:WorkerOwnerState::Stopping;}
        const bool settled=process.terminate(250);
        complete(!settled?WorkerOwnerState::Unreaped:fault?WorkerOwnerState::Faulted:WorkerOwnerState::Exited,
            fault||!settled?8:7,process.reaped());
    }
};
WorkerProcessOwner::WorkerProcessOwner():impl_(new Impl){}
WorkerProcessOwner::~WorkerProcessOwner(){stop();}
bool WorkerProcessOwner::start(std::filesystem::path exe,std::vector<std::string> args,WorkerCommand initial,unsigned drain){
    auto &p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
    if(p.attempted||p.cancel.load())return false;
    p.attempted=true;
    size_t bytes=0;for(const auto &arg:args)bytes+=arg.size();
    if(args.size()>256||bytes>24000||exe.native().size()>4096||!validWorkerCommand(initial)||initial.revision!=1||initial.directive!=WorkerDirective::Pause||drain>65000){p.view.state=WorkerOwnerState::Faulted;p.view.revoked=true;p.view.done=true;p.view.exitCode=8;return false;}
    p.binding=initial.binding;p.view.state=WorkerOwnerState::Starting;
    try{p.thread=std::thread([&p,exe=std::move(exe),args=std::move(args),initial=std::move(initial),drain]()mutable{p.run(std::move(exe),std::move(args),std::move(initial),drain);});}
    catch(...){p.view.state=WorkerOwnerState::Faulted;p.view.revoked=true;p.view.done=true;p.view.exitCode=8;return false;}return true;
}
bool WorkerProcessOwner::submit(const WorkerCommand &command){
    auto &p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);const auto now=workerControlNowMs();
    if(p.cancel.load()||p.view.state!=WorkerOwnerState::Running||p.pending||!validWorkerCommand(command)||command.issuedMonoMs>now||now>=command.deadlineMonoMs)return false;
    const auto &a=p.binding,&b=command.binding;
    if(a.sessionKey!=b.sessionKey||a.workerKey!=b.workerKey||a.epoch!=b.epoch||a.slot!=b.slot||a.workerGeneration!=b.workerGeneration)return false;
    p.pending=command;return true;
}
WorkerOwnerSnapshot WorkerProcessOwner::snapshot()const{std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->view;}
std::vector<std::string> WorkerProcessOwner::takeLines(){std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<std::string> out;out.reserve(impl_->lines.size());for(auto &line:impl_->lines)out.push_back(std::move(line));impl_->lines.clear();impl_->bytes=0;return out;}
void WorkerProcessOwner::requestStop(){
    auto &p=*impl_;p.cancel=true;std::lock_guard<std::mutex> lock(p.mutex);p.attempted=true;p.view.revoked=true;
    if(p.view.state==WorkerOwnerState::Starting||p.view.state==WorkerOwnerState::Running)p.view.state=WorkerOwnerState::Stopping;
    if(p.view.state==WorkerOwnerState::Dormant){p.view.state=WorkerOwnerState::Exited;p.view.done=true;p.view.exitCode=0;}
}
void WorkerProcessOwner::stop(){requestStop();if(impl_->thread.joinable())impl_->thread.join();}
} // namespace om
