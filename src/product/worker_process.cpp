#include "worker_process.h"
#include "native_process_admission.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <deque>
#include <limits>
#include <thread>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>
extern char **environ;
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2,40)
#define OM_WORKER_PIDFD_SPAWN 1
#endif
#endif
#endif
namespace om {
namespace {
using Clock=std::chrono::steady_clock;
enum class IO { Ready, Pending, Closed, Failed };
bool integer(const char *s,uint64_t &out){
    if(!s||!*s)return false;
    const std::string_view v(s);
    if(v.size()>20||(v.size()>1&&v[0]=='0')||v[0]<'0'||v[0]>'9')return false;
    const auto parsed=std::from_chars(v.data(),v.data()+v.size(),out);
    return parsed.ec==std::errc{}&&parsed.ptr==v.data()+v.size()&&out;
}
bool sameBinding(const WorkerControlBinding &a,const WorkerControlBinding &b){
    return a.sessionKey==b.sessionKey&&a.workerKey==b.workerKey&&a.epoch==b.epoch&&a.slot==b.slot&&a.workerGeneration==b.workerGeneration;
}
bool live(const WorkerCommand &c,uint64_t now){return validWorkerCommand(c)&&c.issuedMonoMs<=now&&now<c.deadlineMonoMs;}
#if defined(_WIN32)
using Native=HANDLE;const Native invalid=INVALID_HANDLE_VALUE;
void closeNative(Native &h){if(h&&h!=invalid)CloseHandle(h);h=invalid;}
bool nonblocking(Native h){DWORD mode=PIPE_READMODE_BYTE|PIPE_NOWAIT;return SetNamedPipeHandleState(h,&mode,nullptr,nullptr)!=0;}
bool inherit(Native h,bool on){return SetHandleInformation(h,HANDLE_FLAG_INHERIT,on?HANDLE_FLAG_INHERIT:0)!=0;}
IO readBytes(Native h,char *bytes,size_t capacity,size_t &count){
    DWORD n=0;const auto ok=ReadFile(h,bytes,static_cast<DWORD>(capacity),&n,nullptr);count=n;
    if(ok)return n?IO::Ready:IO::Closed;
    const auto error=GetLastError();return error==ERROR_NO_DATA?IO::Pending:error==ERROR_BROKEN_PIPE?IO::Closed:IO::Failed;
}
IO writeBytes(Native h,const char *bytes,size_t size,size_t &count){
    DWORD n=0;const auto ok=WriteFile(h,bytes,static_cast<DWORD>(size),&n,nullptr);count=n;
    if(ok)return n?IO::Ready:IO::Pending;
    const auto error=GetLastError();return error==ERROR_BROKEN_PIPE||error==ERROR_NO_DATA?IO::Closed:IO::Failed;
}
std::wstring quote(const std::wstring &value){
    std::wstring out=L"\"";size_t slashes=0;
    for(const wchar_t c:value){if(c==L'\\'){++slashes;continue;}
        out.append(slashes*(c==L'"'?2:1)+(c==L'"'?1:0),L'\\');slashes=0;out+=c;}
    out.append(slashes*2,L'\\');return out+L'"';
}
#else
using Native=int;constexpr Native invalid=-1;
void closeNative(Native &fd){if(fd>=0)::close(fd);fd=-1;}
bool nonblocking(Native fd){const int flags=fcntl(fd,F_GETFL);return flags>=0&&fcntl(fd,F_SETFL,flags|O_NONBLOCK)==0;}
IO readBytes(Native fd,char *bytes,size_t capacity,size_t &count){
    const auto n=::read(fd,bytes,capacity);count=n>0?static_cast<size_t>(n):0;
    if(n>=0)return n?IO::Ready:IO::Closed;
    return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR?IO::Pending:IO::Failed;
}
IO writeBytes(Native fd,const char *bytes,size_t size,size_t &count){
    const auto n=::send(fd,bytes,size,MSG_DONTWAIT|MSG_NOSIGNAL);count=n>0?static_cast<size_t>(n):0;
    if(n>=0)return n?IO::Ready:IO::Closed;
    return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR?IO::Pending:errno==EPIPE||errno==ECONNRESET?IO::Closed:IO::Failed;
}
#endif
} // namespace
namespace worker_process_detail {
struct Channel {
    Native input=invalid,output=invalid;
    std::string pending;size_t offset=0;uint64_t deadline=0,last=0;
    ~Channel(){close();}
    void close(){if(output==input)output=invalid;closeNative(input);closeNative(output);} // retain unresolved bytes for inspection/lifetime
    bool queue(const std::string &wire,uint64_t until){
        if(input==invalid||output==invalid||!pending.empty()||wire.empty()||wire.size()>workerControlFrameLimit)return false;
        pending=wire;offset=0;deadline=until;return true;
    }
    bool flush(uint64_t now){
        if(!now||now<last)return false;
        last=now;
        if(pending.empty())return true;
        if(now>=deadline)return false;
        size_t count=0;const auto result=writeBytes(output,pending.data()+offset,pending.size()-offset,count);
        if(result==IO::Closed||result==IO::Failed||count>pending.size()-offset)return false;
        offset+=count;if(offset==pending.size()){pending.clear();offset=0;}return true;
    }
};
} // namespace worker_process_detail
namespace {
bool pathAllowed(const std::filesystem::path &p){
    if(!p.is_absolute()||p.native().empty()||p.native().size()>4096||p.native().find(static_cast<std::filesystem::path::value_type>(0))!=p.native().npos)return false;
#if defined(_WIN32)
    const auto root=p.root_name().native();if(root.size()!=2||root[1]!=L':'||p.native().find(L'"')!=p.native().npos||p.native().find(L':',2)!=p.native().npos)return false;
#endif
    return true;
}
}
bool parseWorkerChannelHandles(const char *input,const char *output,const char *parent,WorkerChannelHandles *out){
    WorkerChannelHandles h;if(!out||!integer(input,h.input)||!integer(output,h.output)||!integer(parent,h.parent))return false;
#if defined(_WIN32)
    if(h.input>UINTPTR_MAX||h.output>UINTPTR_MAX||h.parent>UINTPTR_MAX||h.input==h.output||h.input==h.parent||h.output==h.parent)return false;
#else
    if(h.input!=3||h.output!=3||h.parent>static_cast<uint64_t>(std::numeric_limits<pid_t>::max()))return false;
#endif
    *out=h;return true;
}
struct WorkerControlClient::Impl {
    mutable std::mutex mutex;std::unique_ptr<WorkerControlGate> gate;
    mutable bool fault=false;bool attempted=false,held=false;
    std::atomic<bool> cancel{false};std::thread thread;worker_process_detail::Channel channel;WorkerControlFramer frames;
    uint64_t startup=0;mutable uint64_t last=0;
    bool workEnabled=false,workFinished=false;uint64_t workSequence=0,workFlushed=0;
    struct WorkFrame {std::string wire;uint64_t deadline,sequence;};
    std::deque<WorkFrame> work; // at most 32 immutable originals; no overwrite, decimation or restamping
#if defined(_WIN32)
    Native parent=invalid;
    ~Impl(){closeNative(parent);}
    bool parentAlive()const{return WaitForSingleObject(parent,0)==WAIT_TIMEOUT;}
#else
    pid_t parent=-1;
    bool parentAlive()const{return getppid()==parent;}
#endif
    WorkControlRequest state()const{
        if(cancel.load())return WorkControlRequest::Stop;
        if(fault)return WorkControlRequest::Fault;
        const auto now=workerControlNowMs();
        if(!attempted||!now||now<last||(!gate&&now>=startup)){fault=true;return WorkControlRequest::Fault;}last=now;
        return gate?gate->requestState():WorkControlRequest::Pause;
    }
    void fail(){std::lock_guard<std::mutex> lock(mutex);fault=true;if(gate)gate->fail();}
    void run(){
        try{std::string sent;uint64_t sendingWork=0;
            while(!cancel.load()){
                if(!parentAlive()){cancel=true;break;}
                char bytes[workerControlChunkLimit];size_t count=0;const auto read=readBytes(channel.input,bytes,sizeof(bytes),count);
                if(read==IO::Closed){cancel=true;break;}if(read==IO::Failed){fail();break;}
                if(count&&!frames.feed(std::string_view(bytes,count),[&](std::string_view frame){
                    WorkerCommand c;if(!decodeWorkerCommand(frame,&c))return false;
                    std::lock_guard<std::mutex> lock(mutex);
                    if(state()==WorkControlRequest::Fault||cancel.load())return false;
                    if(!gate){if(c.revision!=1||c.directive!=WorkerDirective::Pause)return false;gate.reset(new WorkerControlGate(c.binding));}
                    return gate->receive(c)!=WorkerCommandResult::Rejected;
                })){fail();break;}
                WorkerControlSnapshot view;
                {std::lock_guard<std::mutex> lock(mutex);const auto current=state();
                    if(current==WorkControlRequest::Stop||current==WorkControlRequest::Fault)break;
                    if(gate)view=gate->snapshot();}
                if(!channel.flush(workerControlNowMs())){fail();break;}
                if(sendingWork&&channel.pending.empty()){std::lock_guard<std::mutex> lock(mutex);workFlushed=sendingWork;sendingWork=0;}
                if(channel.pending.empty()&&view.receipt){std::string wire;
                    if(!encodeWorkerReceipt(*view.receipt,&wire)){fail();break;}
                    if(wire!=sent){if(!channel.queue(wire,view.receipt->command.deadlineMonoMs)){fail();break;}sent=std::move(wire);}
                    else if(workEnabled){std::optional<WorkFrame> next;
                        {std::lock_guard<std::mutex> lock(mutex);if(!cancel.load()&&!fault&&!work.empty()){next=std::move(work.front());work.pop_front();}}
                        if(next){if(!channel.queue(next->wire,next->deadline)){fail();break;}sendingWork=next->sequence;}}}
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }catch(...){fail();}
        if(cancel.load()){std::lock_guard<std::mutex> lock(mutex);if(gate)gate->requestStop();}
        channel.close();
    }
};
WorkerControlClient::WorkerControlClient(bool work):impl_(new Impl){impl_->workEnabled=work;}
WorkerControlClient::~WorkerControlClient(){stop();}
bool WorkerControlClient::adopt(WorkerChannelHandles h){
    auto &p=*impl_;if(p.attempted||p.cancel.load())return false;p.attempted=true;
    WorkerChannelHandles checked;
    if(!parseWorkerChannelHandles(std::to_string(h.input).c_str(),std::to_string(h.output).c_str(),std::to_string(h.parent).c_str(),&checked)){p.fault=true;return false;}
#if defined(_WIN32)
    const auto r=reinterpret_cast<HANDLE>(static_cast<uintptr_t>(h.input)),w=reinterpret_cast<HANDLE>(static_cast<uintptr_t>(h.output));
    const auto parent=reinterpret_cast<HANDLE>(static_cast<uintptr_t>(h.parent));
    DWORD rs=0,ws=0,rpid=0,wpid=0;const DWORD pid=GetProcessId(parent);
    BOOL inJob=FALSE;
    if(!pid||pid==GetCurrentProcessId()||WaitForSingleObject(parent,0)!=WAIT_TIMEOUT||
        GetFileType(r)!=FILE_TYPE_PIPE||GetFileType(w)!=FILE_TYPE_PIPE||
        !GetNamedPipeHandleStateW(r,&rs,nullptr,nullptr,nullptr,nullptr,0)||!GetNamedPipeHandleStateW(w,&ws,nullptr,nullptr,nullptr,nullptr,0)||
        !(rs&PIPE_NOWAIT)||!(ws&PIPE_NOWAIT)||!GetNamedPipeServerProcessId(r,&rpid)||!GetNamedPipeServerProcessId(w,&wpid)||rpid!=pid||wpid!=pid||
        !IsProcessInJob(GetCurrentProcess(),nullptr,&inJob)||!inJob){p.fault=true;return false;}
    p.channel.input=r;p.channel.output=w;p.parent=parent;
    if(!inherit(r,false)||!inherit(w,false)||!inherit(parent,false)){p.fault=true;p.channel.close();closeNative(p.parent);return false;}
#else
    struct ucred peer{};socklen_t length=sizeof(peer);int type=0;socklen_t typeLength=sizeof(type);
    if(getppid()!=static_cast<pid_t>(h.parent)||getsockopt(3,SOL_SOCKET,SO_PEERCRED,&peer,&length)!=0||length!=sizeof(peer)||
        peer.pid!=getppid()||peer.uid!=getuid()||getsockopt(3,SOL_SOCKET,SO_TYPE,&type,&typeLength)!=0||type!=SOCK_STREAM||
        prctl(PR_SET_PDEATHSIG,SIGKILL)!=0||getppid()!=static_cast<pid_t>(h.parent)){p.fault=true;return false;}
    p.parent=static_cast<pid_t>(h.parent);p.channel.input=p.channel.output=3;
    if(fcntl(3,F_SETFD,FD_CLOEXEC)!=0||!nonblocking(3)){p.fault=true;p.channel.close();return false;}
#endif
    p.last=workerControlNowMs();if(!p.last||p.last>UINT64_MAX-workerControlMaxLeaseMs){p.fault=true;p.channel.close();return false;}
    p.startup=p.last+workerControlMaxLeaseMs;
    try{p.thread=std::thread([&p]{p.run();});}catch(...){p.fault=true;p.channel.close();return false;}return true;
}
void WorkerControlClient::requestStop(){impl_->cancel=true;std::lock_guard<std::mutex> lock(impl_->mutex);if(impl_->gate)impl_->gate->requestStop();}
void WorkerControlClient::stop(){requestStop();if(impl_->thread.joinable())impl_->thread.join();}
WorkerControlSnapshot WorkerControlClient::snapshot()const{
    std::lock_guard<std::mutex> lock(impl_->mutex);const auto state=impl_->state();WorkerControlSnapshot out;
    if(impl_->gate)out=impl_->gate->snapshot();
    if(state==WorkControlRequest::Stop)out.state=WorkerControlState::Stopped;
    else if(impl_->fault)out.state=WorkerControlState::Faulted;
    return out;
}
bool WorkerControlClient::publishWork(const WorkerWorkEvent &event)noexcept{
    auto &p=*impl_;
    try{std::string wire;const bool valid=encodeWorkerWorkEvent(event,&wire);
        std::lock_guard<std::mutex> lock(p.mutex);const auto state=p.state();
        if(!p.workEnabled||state==WorkControlRequest::Stop||state==WorkControlRequest::Fault)return false;
        const auto view=p.gate?p.gate->snapshot():WorkerControlSnapshot{};
        if(!valid||!view.receipt||!sameBinding(event.binding,view.receipt->command.binding)||p.workFinished||p.workSequence==UINT64_MAX||
            event.sequence!=p.workSequence+1||(p.workSequence==0)!=(event.kind==WorkerWorkKind::Start)||
            event.observed.monotonicMs>p.last||p.work.size()>=32){p.fault=true;if(p.gate)p.gate->fail();return false;}
        p.work.push_back({std::move(wire),view.receipt->command.deadlineMonoMs,event.sequence});p.workSequence=event.sequence;
        p.workFinished=event.kind==WorkerWorkKind::Finish;return true;
    }catch(...){p.fail();return false;}
}
uint64_t WorkerControlClient::flushedWorkSequence()const{std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->workFlushed;}
WorkControlRequest WorkerControlClient::requestState()const noexcept{std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->state();}
bool WorkerControlClient::pauseRequested()const noexcept{return requestState()!=WorkControlRequest::Run;}
void WorkerControlClient::pauseBoundary(bool held)noexcept{std::lock_guard<std::mutex> lock(impl_->mutex);impl_->held=held;if(impl_->gate)impl_->gate->pauseBoundary(held);}
void WorkerControlClient::pauseHeld()noexcept{std::lock_guard<std::mutex> lock(impl_->mutex);if(impl_->held&&impl_->gate)impl_->gate->pauseBoundary(true);}
WorkControlRequest WorkerControlClient::applyWorkWait(unsigned &ms)noexcept{std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto state=impl_->state();return state==WorkControlRequest::Run&&impl_->gate?impl_->gate->applyWorkWait(ms):state;}

struct WorkerControlProcess::Impl {
    bool attempted=false,owned=false,settled=false,unknown=false,reserved=false,blocked=false,controlFault=false,verified=false;
    WorkerProcessState state=WorkerProcessState::Dormant;int exit=-1;
    WorkerControlBinding binding;worker_process_detail::Channel channel;Native logs=invalid;WorkerControlFramer frames;
    bool workEnabled=false,workFinished=false,workClosed=false;uint64_t workSequence=0;
    NativeProcessAdmission admission;
#if defined(_WIN32)
    Native process=invalid,job=invalid;
    bool jobBound=false;
#else
    int pidfd=-1;
#endif
    static std::mutex &registryMutex(){static std::mutex mutex;return mutex;}
    static unsigned &reservations(){static unsigned count=0;return count;}
    static std::atomic<unsigned> &unresolved(){static std::atomic<unsigned> count{0};return count;}
    static std::vector<Impl *> &quarantine(){static auto *items=new std::vector<Impl *>;return *items;} // retained until proven exit or OS process teardown
    ~Impl(){channel.close();closeNative(logs);
#if defined(_WIN32)
        closeNative(process);closeNative(job);
#else
        closeNative(pidfd);
#endif
        if(reserved)--reservations(); // caller serializes registry operations
    }
    void unreaped(){state=WorkerProcessState::Unreaped;if(!blocked){blocked=true;++unresolved();admission.markUnresolved();}}
    bool observe(){
        if(!owned||settled)return settled;
        if(unknown)return false; // lost reaping ownership is terminal; never probe a potentially reused PID
#if defined(_WIN32)
        const auto wait=WaitForSingleObject(process,0);
        if(wait==WAIT_TIMEOUT){verified=true;return false;}
        DWORD code=0;if(wait!=WAIT_OBJECT_0||!GetExitCodeProcess(process,&code)){unknown=true;unreaped();return false;}
        exit=static_cast<int>(code);
#else
#if defined(OM_WORKER_PIDFD_SPAWN)
        siginfo_t info{};const int result=waitid(P_PIDFD,static_cast<id_t>(pidfd),&info,WEXITED|WNOHANG);
        if(result<0&&errno==EINTR)return false;
        if(result||(info.si_pid&&info.si_code!=CLD_EXITED&&info.si_code!=CLD_KILLED&&info.si_code!=CLD_DUMPED)){unknown=true;unreaped();return false;}
        verified=true;if(!info.si_pid)return false;
        exit=info.si_code==CLD_EXITED?info.si_status:128+info.si_status;
#else
        unknown=true;unreaped();return false;
#endif
#endif
        settled=true;if(blocked){blocked=false;--unresolved();}admission.settled();state=WorkerProcessState::Exited;
        // An opted-in raw stream may still have buffered originals after actual reaping. Drain it with bounded later polls.
        if(!workEnabled||workClosed)channel.close();
        return true;
    }
    static bool reserve(Impl *p){
        std::lock_guard<std::mutex> lock(registryMutex());auto &held=quarantine();
        if(held.capacity()<256)held.reserve(256); // retain without allocating during uncertain teardown
        for(auto i=held.begin();i!=held.end();){if((*i)->observe()){delete *i;i=held.erase(i);}else ++i;}
        if(!held.empty()||unresolved().load()||reservations()>=256)return false;
        if(!p->admission.begin())return false;
        ++reservations();return true;
    }
    bool reap(unsigned ms){
        channel.close();if(!owned||observe())return true;
        if(unknown){unreaped();return false;}
        const auto until=Clock::now()+std::chrono::milliseconds(ms);bool signalled=false;
        do{
        if(verified&&!signalled){
#if defined(_WIN32)
            if(jobBound)TerminateJobObject(job,8);else TerminateProcess(process,8);
#else
#if defined(SYS_pidfd_send_signal)
            ::syscall(SYS_pidfd_send_signal,pidfd,SIGKILL,nullptr,0u);
#endif
#endif
            signalled=true;
        }
        if(observe())return true;
        if(unknown||Clock::now()>=until)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));}while(true);
        unreaped();return false;
    }
    void fail(){channel.close();controlFault=true;state=WorkerProcessState::Failed;}
    bool launch(const std::filesystem::path &exe,const std::vector<std::string> &args,const std::atomic<bool> &cancel){
#if defined(_WIN32)
        struct Temps {Native childIn=invalid,childOut=invalid,logWrite=invalid,null=invalid,parent=invalid,thread=invalid;
            ~Temps(){closeNative(childIn);closeNative(childOut);closeNative(logWrite);closeNative(null);closeNative(parent);closeNative(thread);}} temp;
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
        if(!CreatePipe(&temp.childIn,&channel.output,&sa,4096)||!CreatePipe(&channel.input,&temp.childOut,&sa,4096)||!CreatePipe(&logs,&temp.logWrite,&sa,8192))return false;
        bool ready=inherit(channel.input,false)&&inherit(channel.output,false)&&inherit(logs,false)&&
            nonblocking(temp.childIn)&&nonblocking(temp.childOut)&&nonblocking(channel.input)&&nonblocking(channel.output)&&nonblocking(logs);
        temp.null=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
        ready=ready&&temp.null!=invalid&&DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&temp.parent,SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,TRUE,0);
        job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION policy{};
        policy.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_ACTIVE_PROCESS;policy.BasicLimitInformation.ActiveProcessLimit=1;
        ready=ready&&job&&SetInformationJobObject(job,JobObjectExtendedLimitInformation,&policy,sizeof(policy));
        SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);std::vector<unsigned char> buffer(size);
        auto *attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
        const bool initialized=ready&&InitializeProcThreadAttributeList(attributes,1,0,&size);
        struct AttributeCleanup {LPPROC_THREAD_ATTRIBUTE_LIST value;~AttributeCleanup(){if(value)DeleteProcThreadAttributeList(value);}} cleanup{initialized?attributes:nullptr};
        HANDLE handles[]={temp.childIn,temp.childOut,temp.logWrite,temp.null,temp.parent};
        ready=initialized&&UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr);
        std::wstring command=quote(exe.wstring());for(const auto &arg:args)command+=L" "+quote(std::filesystem::u8path(arg).wstring());
        command+=L" --worker-control-v1 "+std::to_wstring(reinterpret_cast<uintptr_t>(temp.childIn))+L" "+std::to_wstring(reinterpret_cast<uintptr_t>(temp.childOut))+L" "+std::to_wstring(reinterpret_cast<uintptr_t>(temp.parent));
        std::vector<wchar_t> cmd(command.begin(),command.end());cmd.push_back(0);
        STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput=temp.null;si.StartupInfo.hStdOutput=si.StartupInfo.hStdError=temp.logWrite;si.lpAttributeList=attributes;
        PROCESS_INFORMATION pi{};
        const bool created=ready&&!cancel.load()&&admission.permitted()&&CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,
            EXTENDED_STARTUPINFO_PRESENT|CREATE_SUSPENDED,nullptr,nullptr,&si.StartupInfo,&pi);
        if(!created)return false;
        owned=true;process=pi.hProcess;temp.thread=pi.hThread;jobBound=AssignProcessToJobObject(job,process)!=0;
        return jobBound&&!cancel.load()&&admission.permitted()&&ResumeThread(temp.thread)!=static_cast<DWORD>(-1);
#else
        struct sigaction action{};if(sigaction(SIGCHLD,nullptr,&action)!=0||action.sa_handler!=SIG_DFL||(action.sa_flags&SA_NOCLDWAIT))return false;
#if !defined(OM_WORKER_PIDFD_SPAWN) || !defined(SYS_pidfd_send_signal)
        return false;
#endif
        struct Pipes {int pair[2]={-1,-1},output[2]={-1,-1},child=-1,logWrite=-1;
            ~Pipes(){for(int fd:{pair[0],pair[1],output[0],output[1],child,logWrite})if(fd>=0)::close(fd);}} pipe;
        if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pipe.pair)!=0||pipe2(pipe.output,O_CLOEXEC)!=0)return false;
        // Move child sources above the destination descriptor 3 before composing spawn actions.
        pipe.child=fcntl(pipe.pair[1],F_DUPFD_CLOEXEC,4);pipe.logWrite=fcntl(pipe.output[1],F_DUPFD_CLOEXEC,4);
        closeNative(pipe.pair[1]);closeNative(pipe.output[1]);channel.input=channel.output=pipe.pair[0];pipe.pair[0]=-1;logs=pipe.output[0];pipe.output[0]=-1;
        bool ready=pipe.child>=0&&pipe.logWrite>=0&&nonblocking(channel.input)&&nonblocking(pipe.child)&&nonblocking(logs);
        struct Setup {posix_spawn_file_actions_t actions;posix_spawnattr_t attributes;bool actionsReady=false,attributesReady=false;
            ~Setup(){if(attributesReady)posix_spawnattr_destroy(&attributes);if(actionsReady)posix_spawn_file_actions_destroy(&actions);}} setup;
        if(posix_spawn_file_actions_init(&setup.actions)!=0)return false;
        setup.actionsReady=true;
        if(posix_spawnattr_init(&setup.attributes)!=0)return false;
        setup.attributesReady=true;
        ready=ready&&posix_spawn_file_actions_addopen(&setup.actions,0,"/dev/null",O_RDONLY,0)==0&&
            posix_spawn_file_actions_adddup2(&setup.actions,pipe.logWrite,1)==0&&posix_spawn_file_actions_adddup2(&setup.actions,pipe.logWrite,2)==0&&
            posix_spawn_file_actions_adddup2(&setup.actions,pipe.child,3)==0;
#if defined(OM_WORKER_PIDFD_SPAWN)
        ready=ready&&posix_spawn_file_actions_addclosefrom_np(&setup.actions,4)==0;
#endif
        sigset_t mask,defaults;sigemptyset(&mask);sigemptyset(&defaults);for(int sig:{SIGPIPE,SIGINT,SIGTERM})sigaddset(&defaults,sig);
        ready=ready&&posix_spawnattr_setflags(&setup.attributes,POSIX_SPAWN_SETPGROUP|POSIX_SPAWN_SETSIGMASK|POSIX_SPAWN_SETSIGDEF)==0&&
            posix_spawnattr_setpgroup(&setup.attributes,0)==0&&posix_spawnattr_setsigmask(&setup.attributes,&mask)==0&&posix_spawnattr_setsigdefault(&setup.attributes,&defaults)==0;
        std::vector<std::string> words{exe.string()};words.insert(words.end(),args.begin(),args.end());
        words.insert(words.end(),{"--worker-control-v1","3","3",std::to_string(getpid())});
        std::vector<char *> argv;for(auto &word:words)argv.push_back(&word[0]);argv.push_back(nullptr);
#if defined(OM_WORKER_PIDFD_SPAWN)
        const int result=ready&&!cancel.load()&&admission.permitted()?pidfd_spawn(&pidfd,exe.c_str(),&setup.actions,&setup.attributes,argv.data(),environ):-1;
#else
        const int result=-1;(void)ready;(void)cancel;
#endif
        if(result!=0){pidfd=-1;return false;}owned=true;return true;
#endif
    }
};
WorkerControlProcess::WorkerControlProcess():impl_(new Impl){}
WorkerControlProcess::~WorkerControlProcess(){
    if(!impl_->reap(250)){std::lock_guard<std::mutex> lock(Impl::registryMutex());Impl::quarantine().push_back(impl_.release());}
    else{std::lock_guard<std::mutex> lock(Impl::registryMutex());impl_.reset();}
}
bool WorkerControlProcess::start(const std::filesystem::path &exe,const std::vector<std::string> &args,const WorkerCommand &initial,const std::atomic<bool> &cancel,bool work){
    auto &p=*impl_;if(p.attempted)return false;p.attempted=true;
    size_t total=0;bool valid=args.size()<=256;
    for(const auto &arg:args){valid=valid&&!arg.empty()&&arg.size()<=4096&&arg.find('\0')==arg.npos&&arg!="--worker-control-v1";total+=arg.size();}
    if(cancel.load()||!valid||total>24000||!pathAllowed(exe)||!live(initial,workerControlNowMs())||initial.revision!=1||initial.directive!=WorkerDirective::Pause){
        p.state=WorkerProcessState::Failed;return false;}
    try{
        if(!Impl::reserve(&p)){p.state=nativeProcessUnresolved()?WorkerProcessState::Unreaped:WorkerProcessState::Failed;return false;}
        p.reserved=true;p.binding=initial.binding;p.workEnabled=work;
        if(!p.launch(exe,args,cancel)||cancel.load()||!p.admission.permitted()){p.fail();p.reap(250);return false;}
        p.state=WorkerProcessState::Running;
        if(!submit(initial)){p.fail();p.reap(250);return false;}
        return true;
    }catch(...){p.fail();p.reap(250);return false;}
}
bool WorkerControlProcess::submit(const WorkerCommand &command){
    auto &p=*impl_;std::string wire;
    if(p.state!=WorkerProcessState::Running||!p.admission.permitted()||!sameBinding(command.binding,p.binding)||!live(command,workerControlNowMs())||!encodeWorkerCommand(command,&wire))return false;
    return p.channel.queue(wire,command.deadlineMonoMs);
}
WorkerProcessPoll WorkerControlProcess::poll(){
    auto &p=*impl_;WorkerProcessPoll out;
    if(p.state==WorkerProcessState::Running&&!p.admission.permitted())p.fail();
    if(p.state==WorkerProcessState::Running){
        if(!p.channel.flush(workerControlNowMs()))p.fail();}
    if(p.state==WorkerProcessState::Running||(p.state==WorkerProcessState::Exited&&p.workEnabled&&!p.workClosed&&p.channel.input!=invalid)){
        {char bytes[workerControlChunkLimit];size_t count=0;
            // A burst of small raw events must not accidentally exceed the unchanged 16-frame parser budget.
            const auto result=readBytes(p.channel.input,bytes,p.workEnabled?workerControlFrameLimit:sizeof(bytes),count);
            if(result==IO::Failed)p.fail();
            else if(result==IO::Closed){p.workClosed=true;if(!p.frames.complete())p.fail();else{p.channel.close();if(!p.settled)p.state=WorkerProcessState::Disconnected;}}
            else if(count&&!p.frames.feed(std::string_view(bytes,count),[&](std::string_view frame){WorkerReceipt r;
                if(frame.substr(0,6)=="CMWW1 "){WorkerWorkEvent e;
                    if(!p.workEnabled||p.workFinished||!decodeWorkerWorkEvent(frame,&e)||!sameBinding(e.binding,p.binding)||p.workSequence==UINT64_MAX||
                        e.sequence!=p.workSequence+1||(p.workSequence==0)!=(e.kind==WorkerWorkKind::Start)||e.observed.monotonicMs>workerControlNowMs())return false;
                    p.workSequence=e.sequence;p.workFinished=e.kind==WorkerWorkKind::Finish;out.work.push_back(std::move(e));return true;}
                if(!decodeWorkerReceipt(frame,&r)||!sameBinding(r.command.binding,p.binding))return false;
                out.receipts.push_back(std::move(r));return true;})){out.work.clear();p.fail();}}}
    if(p.logs!=invalid){char bytes[8192];size_t count=0;const auto result=readBytes(p.logs,bytes,sizeof(bytes),count);
        if(count)out.output.assign(bytes,count);
        if(result==IO::Closed||result==IO::Failed)closeNative(p.logs);}
    const bool streamEnded=p.state==WorkerProcessState::Running||p.state==WorkerProcessState::Disconnected||p.state==WorkerProcessState::Failed;
    if(p.owned)p.observe();
    if(p.admission.invalidated()){p.controlFault=true;p.channel.close();out.receipts.clear();out.work.clear();if(p.state!=WorkerProcessState::Unreaped&&!p.settled)p.state=WorkerProcessState::Failed;}
    if(streamEnded&&p.settled&&(!p.workEnabled||p.workClosed)&&!p.frames.complete())p.controlFault=true; // actual exit cannot erase an incomplete/malformed receipt stream
    out.state=p.state;out.exitCode=p.exit;out.controlFault=p.controlFault;
    out.workStreamClosed=p.workClosed;out.workStreamComplete=p.workEnabled&&p.workClosed&&p.workFinished&&p.frames.complete()&&!p.controlFault;return out;
}
void WorkerControlProcess::requestStop(){auto &p=*impl_;p.attempted=true;p.channel.close(); // intentional cancellation leaves any unconsumed tail incomplete
    if(p.state!=WorkerProcessState::Exited&&p.state!=WorkerProcessState::Unreaped)p.state=WorkerProcessState::Stopping;}
bool WorkerControlProcess::terminate(unsigned ms){return impl_->reap((std::min)(ms,2000u));}
bool WorkerControlProcess::reaped()const{return impl_->settled;}
} // namespace om
