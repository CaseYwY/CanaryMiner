#include "worker_process.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
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
#include <unistd.h>
extern char **environ;
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
    std::atomic<bool> cancel{false};std::thread thread;Channel channel;WorkerControlFramer frames;
    uint64_t startup=0;mutable uint64_t last=0;
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
        try{std::string sent;
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
                if(channel.pending.empty()&&view.receipt){std::string wire;
                    if(!encodeWorkerReceipt(*view.receipt,&wire)){fail();break;}
                    if(wire!=sent){if(!channel.queue(wire,view.receipt->command.deadlineMonoMs)){fail();break;}sent=std::move(wire);}}
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }catch(...){fail();}
        if(cancel.load()){std::lock_guard<std::mutex> lock(mutex);if(gate)gate->requestStop();}
        channel.close();
    }
};
WorkerControlClient::WorkerControlClient():impl_(new Impl){}
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
WorkControlRequest WorkerControlClient::requestState()const noexcept{std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->state();}
bool WorkerControlClient::pauseRequested()const noexcept{return requestState()!=WorkControlRequest::Run;}
void WorkerControlClient::pauseBoundary(bool held)noexcept{std::lock_guard<std::mutex> lock(impl_->mutex);impl_->held=held;if(impl_->gate)impl_->gate->pauseBoundary(held);}
void WorkerControlClient::pauseHeld()noexcept{std::lock_guard<std::mutex> lock(impl_->mutex);if(impl_->held&&impl_->gate)impl_->gate->pauseBoundary(true);}
WorkControlRequest WorkerControlClient::applyWorkWait(unsigned &ms)noexcept{std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto state=impl_->state();return state==WorkControlRequest::Run&&impl_->gate?impl_->gate->applyWorkWait(ms):state;}

struct WorkerControlProcess::Impl {
    bool attempted=false,owned=false,settled=false,unknown=false,reserved=false,blocked=false,controlFault=false;
    WorkerProcessState state=WorkerProcessState::Dormant;int exit=-1;
    WorkerControlBinding binding;Channel channel;Native logs=invalid;WorkerControlFramer frames;
#if defined(_WIN32)
    Native process=invalid,job=invalid;
#else
    pid_t pid=-1;
#endif
    static std::mutex &registryMutex(){static std::mutex mutex;return mutex;}
    static unsigned &reservations(){static unsigned count=0;return count;}
    static std::atomic<unsigned> &unresolved(){static std::atomic<unsigned> count{0};return count;}
    static std::vector<Impl *> &quarantine(){static auto *items=new std::vector<Impl *>;return *items;} // retained until proven exit or OS process teardown
    ~Impl(){channel.close();closeNative(logs);
#if defined(_WIN32)
        closeNative(process);closeNative(job);
#endif
        if(reserved)--reservations(); // caller serializes registry operations
    }
    void unreaped(){state=WorkerProcessState::Unreaped;if(!blocked){blocked=true;++unresolved();}}
    bool observe(){
        if(!owned||settled)return settled;
        if(unknown)return false; // lost reaping ownership is terminal; never probe a potentially reused PID
#if defined(_WIN32)
        const auto wait=WaitForSingleObject(process,0);
        if(wait==WAIT_TIMEOUT)return false;
        DWORD code=0;if(wait!=WAIT_OBJECT_0||!GetExitCodeProcess(process,&code)){unknown=true;unreaped();return false;}
        exit=static_cast<int>(code);
#else
        int status=0;const auto result=waitpid(pid,&status,WNOHANG);
        if(result==0||(result<0&&errno==EINTR))return false;
        if(result!=pid){unknown=true;unreaped();return false;} // no signalling a possibly reused PID after another reaper consumed it
        exit=WIFEXITED(status)?WEXITSTATUS(status):128+(WIFSIGNALED(status)?WTERMSIG(status):0);
#endif
        settled=true;if(blocked){blocked=false;--unresolved();}state=WorkerProcessState::Exited;channel.close();return true;
    }
    static bool reserve(){
        std::lock_guard<std::mutex> lock(registryMutex());auto &held=quarantine();
        for(auto i=held.begin();i!=held.end();){if((*i)->observe()){delete *i;i=held.erase(i);}else ++i;}
        if(!held.empty()||unresolved().load()||reservations()>=256)return false;
        ++reservations();return true;
    }
    bool reap(unsigned ms){
        channel.close();if(!owned||observe())return true;
        if(unknown){unreaped();return false;}
#if defined(_WIN32)
        if(job!=invalid)TerminateJobObject(job,8);else TerminateProcess(process,8);
#else
        ::kill(-pid,SIGKILL);
#endif
        const auto until=Clock::now()+std::chrono::milliseconds(ms);
        do{if(observe())return true;if(Clock::now()>=until)break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}while(true);
        unreaped();return false;
    }
    void fail(){channel.close();controlFault=true;state=WorkerProcessState::Failed;}
    bool launch(const std::filesystem::path &exe,const std::vector<std::string> &args,const std::atomic<bool> &cancel){
#if defined(_WIN32)
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};Native childIn=invalid,childOut=invalid,logWrite=invalid,null=invalid,parent=invalid;
        auto clean=[&]{closeNative(childIn);closeNative(childOut);closeNative(logWrite);closeNative(null);closeNative(parent);};
        if(!CreatePipe(&childIn,&channel.output,&sa,4096)||!CreatePipe(&channel.input,&childOut,&sa,4096)||!CreatePipe(&logs,&logWrite,&sa,8192)){clean();return false;}
        bool ready=inherit(channel.input,false)&&inherit(channel.output,false)&&inherit(logs,false)&&
            nonblocking(childIn)&&nonblocking(childOut)&&nonblocking(channel.input)&&nonblocking(channel.output)&&nonblocking(logs);
        null=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
        ready=ready&&null!=invalid&&DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&parent,SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,TRUE,0);
        job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION policy{};
        policy.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_ACTIVE_PROCESS;policy.BasicLimitInformation.ActiveProcessLimit=1;
        ready=ready&&job&&SetInformationJobObject(job,JobObjectExtendedLimitInformation,&policy,sizeof(policy));
        SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);std::vector<unsigned char> buffer(size);
        auto *attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
        const bool initialized=ready&&InitializeProcThreadAttributeList(attributes,1,0,&size);
        HANDLE handles[]={childIn,childOut,logWrite,null,parent};
        ready=initialized&&UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr);
        std::wstring command=quote(exe.wstring());for(const auto &arg:args)command+=L" "+quote(std::filesystem::u8path(arg).wstring());
        command+=L" --worker-control-v1 "+std::to_wstring(reinterpret_cast<uintptr_t>(childIn))+L" "+std::to_wstring(reinterpret_cast<uintptr_t>(childOut))+L" "+std::to_wstring(reinterpret_cast<uintptr_t>(parent));
        std::vector<wchar_t> cmd(command.begin(),command.end());cmd.push_back(0);
        STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput=null;si.StartupInfo.hStdOutput=si.StartupInfo.hStdError=logWrite;si.lpAttributeList=attributes;
        PROCESS_INFORMATION pi{};
        const bool created=ready&&!cancel.load()&&!unresolved().load()&&CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,
            EXTENDED_STARTUPINFO_PRESENT|CREATE_SUSPENDED,nullptr,nullptr,&si.StartupInfo,&pi);
        if(initialized)DeleteProcThreadAttributeList(attributes);clean();if(!created)return false;
        owned=true;process=pi.hProcess;
        const bool bound=AssignProcessToJobObject(job,process)!=0;
        const bool resumed=bound&&!cancel.load()&&ResumeThread(pi.hThread)!=static_cast<DWORD>(-1);
        CloseHandle(pi.hThread);if(!resumed){TerminateProcess(process,8);reap(250);return false;}return true;
#else
        struct sigaction action{};if(sigaction(SIGCHLD,nullptr,&action)!=0||action.sa_handler!=SIG_DFL||(action.sa_flags&SA_NOCLDWAIT))return false;
        int pair[2]={-1,-1},output[2]={-1,-1};
        if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair)!=0)return false;
        if(pipe2(output,O_CLOEXEC)!=0){closeNative(pair[0]);closeNative(pair[1]);return false;}
        // Move child sources above the destination descriptor 3 before composing spawn actions.
        const int child=fcntl(pair[1],F_DUPFD_CLOEXEC,4),logWrite=fcntl(output[1],F_DUPFD_CLOEXEC,4);
        closeNative(pair[1]);closeNative(output[1]);channel.input=channel.output=pair[0];logs=output[0];
        auto clean=[&]{if(child>=0)::close(child);if(logWrite>=0)::close(logWrite);};
        bool ready=child>=0&&logWrite>=0&&nonblocking(channel.input)&&nonblocking(child)&&nonblocking(logs);
        posix_spawn_file_actions_t actions;posix_spawnattr_t attributes;
        if(posix_spawn_file_actions_init(&actions)!=0){clean();return false;}
        if(posix_spawnattr_init(&attributes)!=0){posix_spawn_file_actions_destroy(&actions);clean();return false;}
        ready=ready&&posix_spawn_file_actions_addopen(&actions,0,"/dev/null",O_RDONLY,0)==0&&
            posix_spawn_file_actions_adddup2(&actions,logWrite,1)==0&&posix_spawn_file_actions_adddup2(&actions,logWrite,2)==0&&
            posix_spawn_file_actions_adddup2(&actions,child,3)==0;
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2,34)
        ready=ready&&posix_spawn_file_actions_addclosefrom_np(&actions,4)==0;
#else
        ready=false;
#endif
#else
        ready=false;
#endif
        sigset_t mask,defaults;sigemptyset(&mask);sigemptyset(&defaults);for(int sig:{SIGPIPE,SIGINT,SIGTERM})sigaddset(&defaults,sig);
        ready=ready&&posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP|POSIX_SPAWN_SETSIGMASK|POSIX_SPAWN_SETSIGDEF)==0&&
            posix_spawnattr_setpgroup(&attributes,0)==0&&posix_spawnattr_setsigmask(&attributes,&mask)==0&&posix_spawnattr_setsigdefault(&attributes,&defaults)==0;
        std::vector<std::string> words{exe.string()};words.insert(words.end(),args.begin(),args.end());
        words.insert(words.end(),{"--worker-control-v1","3","3",std::to_string(getpid())});
        std::vector<char *> argv;for(auto &word:words)argv.push_back(&word[0]);argv.push_back(nullptr);
        const int result=ready&&!cancel.load()&&!unresolved().load()?posix_spawn(&pid,exe.c_str(),&actions,&attributes,argv.data(),environ):-1;
        posix_spawn_file_actions_destroy(&actions);posix_spawnattr_destroy(&attributes);clean();
        if(result!=0){pid=-1;return false;}owned=true;return true;
#endif
    }
};
WorkerControlProcess::WorkerControlProcess():impl_(new Impl){}
WorkerControlProcess::~WorkerControlProcess(){
    if(!impl_->reap(250)){std::lock_guard<std::mutex> lock(Impl::registryMutex());Impl::quarantine().push_back(impl_.release());}
    else{std::lock_guard<std::mutex> lock(Impl::registryMutex());impl_.reset();}
}
bool WorkerControlProcess::start(const std::filesystem::path &exe,const std::vector<std::string> &args,const WorkerCommand &initial,const std::atomic<bool> &cancel){
    auto &p=*impl_;if(p.attempted)return false;p.attempted=true;
    size_t total=0;bool valid=args.size()<=256;
    for(const auto &arg:args){valid=valid&&!arg.empty()&&arg.size()<=4096&&arg.find('\0')==arg.npos&&arg!="--worker-control-v1";total+=arg.size();}
    if(cancel.load()||!valid||total>24000||!pathAllowed(exe)||!live(initial,workerControlNowMs())||initial.revision!=1||initial.directive!=WorkerDirective::Pause||!Impl::reserve()){
        p.state=WorkerProcessState::Failed;return false;}
    p.reserved=true;p.binding=initial.binding;
    try{if(!p.launch(exe,args,cancel)||cancel.load()){p.fail();p.reap(250);return false;}
        p.state=WorkerProcessState::Running;
        if(!submit(initial)){p.fail();p.reap(250);return false;}
        return true;
    }catch(...){p.fail();p.reap(250);return false;}
}
bool WorkerControlProcess::submit(const WorkerCommand &command){
    auto &p=*impl_;std::string wire;
    if(p.state!=WorkerProcessState::Running||!sameBinding(command.binding,p.binding)||!live(command,workerControlNowMs())||!encodeWorkerCommand(command,&wire))return false;
    return p.channel.queue(wire,command.deadlineMonoMs);
}
WorkerProcessPoll WorkerControlProcess::poll(){
    auto &p=*impl_;WorkerProcessPoll out;
    if(p.state==WorkerProcessState::Running){
        if(!p.channel.flush(workerControlNowMs()))p.fail();
        if(p.state==WorkerProcessState::Running){char bytes[workerControlChunkLimit];size_t count=0;const auto result=readBytes(p.channel.input,bytes,sizeof(bytes),count);
            if(result==IO::Failed)p.fail();
            else if(result==IO::Closed){if(!p.frames.complete())p.fail();else{p.channel.close();p.state=WorkerProcessState::Disconnected;}}
            else if(count&&!p.frames.feed(std::string_view(bytes,count),[&](std::string_view frame){WorkerReceipt r;
                if(!decodeWorkerReceipt(frame,&r)||!sameBinding(r.command.binding,p.binding))return false;
                out.receipts.push_back(std::move(r));return true;}))p.fail();}}
    if(p.logs!=invalid){char bytes[8192];size_t count=0;const auto result=readBytes(p.logs,bytes,sizeof(bytes),count);
        if(count)out.output.assign(bytes,count);
        if(result==IO::Closed||result==IO::Failed)closeNative(p.logs);}
    const bool streamEnded=p.state==WorkerProcessState::Running||p.state==WorkerProcessState::Disconnected||p.state==WorkerProcessState::Failed;
    if(p.owned)p.observe();
    if(streamEnded&&p.settled&&!p.frames.complete())p.controlFault=true; // actual exit cannot erase an incomplete/malformed receipt stream
    out.state=p.state;out.exitCode=p.exit;out.controlFault=p.controlFault;return out;
}
void WorkerControlProcess::requestStop(){auto &p=*impl_;p.attempted=true;p.channel.close();
    if(p.state!=WorkerProcessState::Exited&&p.state!=WorkerProcessState::Unreaped)p.state=WorkerProcessState::Stopping;}
bool WorkerControlProcess::terminate(unsigned ms){return impl_->reap((std::min)(ms,2000u));}
bool WorkerControlProcess::reaped()const{return impl_->settled;}
} // namespace om
