#include "duplex_task_process.h"
#include "native_process_admission.h"
#include <algorithm>
#include <charconv>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>
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
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2,40)
#define OM_DUPLEX_PIDFD_SPAWN 1
#endif
#endif
#endif
namespace om {
namespace {
using IO=DuplexTaskIO;using B=DuplexTaskBytes;using S=DuplexTaskState;using Clock=std::chrono::steady_clock;
#if defined(_WIN32)
using Native=HANDLE;const Native invalid=INVALID_HANDLE_VALUE;
void closeNative(Native &h){if(h&&h!=invalid)CloseHandle(h);h=invalid;}
bool nonblocking(Native h){DWORD mode=PIPE_READMODE_BYTE|PIPE_NOWAIT;return SetNamedPipeHandleState(h,&mode,nullptr,nullptr)!=0;}
bool inherit(Native h,bool on){return SetHandleInformation(h,HANDLE_FLAG_INHERIT,on?HANDLE_FLAG_INHERIT:0)!=0;}
B readBytes(Native h,char *p,std::size_t n){DWORD count=0;const bool ok=ReadFile(h,p,static_cast<DWORD>(n),&count,nullptr)!=0;
    if(ok)return {count?IO::Bytes:IO::Closed,count};const auto error=GetLastError();return {error==ERROR_NO_DATA?IO::Pending:error==ERROR_BROKEN_PIPE?IO::Closed:IO::Error,0};}
B writeBytes(Native h,const char *p,std::size_t n){DWORD count=0;const bool ok=WriteFile(h,p,static_cast<DWORD>(n),&count,nullptr)!=0;
    if(ok)return {count?IO::Bytes:IO::Pending,count};const auto error=GetLastError();return {error==ERROR_NO_DATA||error==ERROR_BROKEN_PIPE?IO::Closed:IO::Error,0};}
#else
using Native=int;constexpr Native invalid=-1;
void closeNative(Native &fd){if(fd>=0)::close(fd);fd=-1;}
bool nonblocking(Native fd){const int flags=fcntl(fd,F_GETFL);return flags>=0&&fcntl(fd,F_SETFL,flags|O_NONBLOCK)==0;}
B readBytes(Native fd,char *p,std::size_t n){const auto count=::read(fd,p,n);if(count>=0)return {count?IO::Bytes:IO::Closed,static_cast<std::size_t>(count)};
    return {errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR?IO::Pending:IO::Error,0};}
B writeBytes(Native fd,const char *p,std::size_t n){const auto count=::send(fd,p,n,MSG_DONTWAIT|MSG_NOSIGNAL);if(count>=0)return {count?IO::Bytes:IO::Closed,static_cast<std::size_t>(count)};
    return {errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR?IO::Pending:errno==EPIPE||errno==ECONNRESET?IO::Closed:IO::Error,0};}
#endif
} // namespace
namespace duplex_task_detail {
struct Channel {
    Native input=invalid,output=invalid;
    ~Channel(){close();}
    void close(){if(input==output)output=invalid;closeNative(input);closeNative(output);}
};
} // namespace duplex_task_detail
namespace {
bool integer(const char *s,uint64_t &out){if(!s)return false;const std::string_view v(s);if(v.empty()||v.size()>20||(v.size()>1&&v[0]=='0')||v[0]<'0'||v[0]>'9')return false;
    const auto r=std::from_chars(v.data(),v.data()+v.size(),out);return r.ec==std::errc{}&&r.ptr==v.data()+v.size()&&out;}
}
struct DuplexTaskProcess::Impl {
    bool attempted=false,owned=false,reaped=false,unknown=false,reserved=false,eof=false,blocked=false,fault=false,verified=false;
    S state=S::Dormant;int exit=-1;duplex_task_detail::Channel channel;std::string pending;std::size_t offset=0;
    NativeProcessAdmission admission;
#if defined(_WIN32)
    Native process=invalid,job=invalid;
    bool jobBound=false;
#else
    int pidfd=-1;
#endif
    static std::mutex &mutex(){static std::mutex value;return value;}
    static std::vector<Impl *> &quarantine(){static auto *value=new std::vector<Impl *>;return *value;}
    static unsigned &reservations(){static unsigned value=0;return value;}
    static std::atomic<unsigned> &unresolved(){static std::atomic<unsigned> value{0};return value;}
    void unreaped(){state=S::Unreaped;if(!blocked){blocked=true;++unresolved();admission.markUnresolved();}}
    ~Impl(){channel.close();
#if defined(_WIN32)
        closeNative(process);closeNative(job);
#else
        closeNative(pidfd);
#endif
        if(reserved)--reservations(); // registry lock belongs to the deleting owner
    }
    bool observe(){if(!owned||reaped)return reaped;if(unknown)return false;
#if defined(_WIN32)
        const auto result=WaitForSingleObject(process,0);if(result==WAIT_TIMEOUT){verified=true;return false;}DWORD code=0;
        if(result!=WAIT_OBJECT_0||!GetExitCodeProcess(process,&code)){unknown=true;unreaped();return false;}exit=static_cast<int>(code);
#else
#if defined(OM_DUPLEX_PIDFD_SPAWN)
        siginfo_t info{};const int result=waitid(P_PIDFD,static_cast<id_t>(pidfd),&info,WEXITED|WNOHANG);
        if(result<0&&errno==EINTR)return false;
        if(result||(info.si_pid&&info.si_code!=CLD_EXITED&&info.si_code!=CLD_KILLED&&info.si_code!=CLD_DUMPED)){unknown=true;unreaped();return false;}
        verified=true;if(!info.si_pid)return false;
        exit=info.si_code==CLD_EXITED?info.si_status:128+info.si_status;
#else
        unknown=true;unreaped();return false;
#endif
#endif
        reaped=true;if(blocked){blocked=false;--unresolved();}admission.settled();state=S::Exited;return true;
    }
    static bool reserve(Impl *p){std::lock_guard<std::mutex> lock(mutex());auto &held=quarantine();
        if(held.capacity()<64)held.reserve(64); // allocate quarantine slots before any process launch, never while tearing one down
        for(auto i=held.begin();i!=held.end();){if((*i)->observe()){delete *i;i=held.erase(i);}else ++i;}
        if(!held.empty()||unresolved().load()||reservations()>=64)return false;
        if(!p->admission.begin())return false;
        ++reservations();return true;}
    bool stop(unsigned ms){channel.close();if(!owned)return true;if(observe())return true;if(unknown){unreaped();return false;}
        bool signalled=false;const auto end=Clock::now()+std::chrono::milliseconds(std::min(ms,2000u));
        do{
        if(verified&&!signalled){
#if defined(_WIN32)
            if(jobBound)TerminateJobObject(job,1);else TerminateProcess(process,1);
#else
#if defined(SYS_pidfd_send_signal)
            ::syscall(SYS_pidfd_send_signal,pidfd,SIGKILL,nullptr,0u);
#endif
#endif
            signalled=true;
        }
        if(observe())return true;
        if(unknown||Clock::now()>=end)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));}while(true);
        unreaped();return false;
    }
    bool launch(const std::filesystem::path &exe,const std::atomic<bool> &cancel,DuplexTaskOutput outputRole){
#if defined(_WIN32)
        struct Temps {Native input=invalid,output=invalid,parent=invalid,null=invalid,thread=invalid,display=invalid,keyboard=invalid;~Temps(){closeNative(input);closeNative(output);closeNative(parent);closeNative(null);closeNative(thread);closeNative(display);closeNative(keyboard);}} child;
        bool console=false;
        if(outputRole!=DuplexTaskOutput::Discard){
            const auto display=GetStdHandle(STD_OUTPUT_HANDLE);DWORD mode=0;
            if(!display||display==invalid||!DuplicateHandle(GetCurrentProcess(),display,GetCurrentProcess(),&child.display,0,TRUE,DUPLICATE_SAME_ACCESS))return false;
            console=GetConsoleMode(child.display,&mode)!=0; // console children must share the existing console, never CREATE_NO_WINDOW
            if(outputRole==DuplexTaskOutput::InheritedTerminal){
                const auto keyboard=GetStdHandle(STD_INPUT_HANDLE);
                if(!console||!keyboard||keyboard==invalid||!DuplicateHandle(GetCurrentProcess(),keyboard,GetCurrentProcess(),&child.keyboard,0,TRUE,DUPLICATE_SAME_ACCESS)||!GetConsoleMode(child.keyboard,&mode))return false;
            }
        }
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
        if(!CreatePipe(&child.input,&channel.output,&sa,8192)||!CreatePipe(&channel.input,&child.output,&sa,8192))return false;
        bool ready=inherit(channel.input,false)&&inherit(channel.output,false)&&nonblocking(channel.input)&&nonblocking(channel.output)&&nonblocking(child.input)&&nonblocking(child.output);
        child.null=CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
        ready=ready&&child.null!=invalid&&DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&child.parent,SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,TRUE,0);
        job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION policy{};policy.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_ACTIVE_PROCESS;policy.BasicLimitInformation.ActiveProcessLimit=1;
        ready=ready&&job&&SetInformationJobObject(job,JobObjectExtendedLimitInformation,&policy,sizeof(policy));
        SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);std::vector<unsigned char> storage(size);auto *attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        const bool initialized=ready&&InitializeProcThreadAttributeList(attributes,1,0,&size);
        struct AttributeCleanup {LPPROC_THREAD_ATTRIBUTE_LIST value;~AttributeCleanup(){if(value)DeleteProcThreadAttributeList(value);}} cleanup{initialized?attributes:nullptr};
        Native handles[]={child.input,child.output,child.parent,child.null,child.display,child.keyboard};const SIZE_T handleBytes=(outputRole==DuplexTaskOutput::InheritedTerminal?6u:outputRole==DuplexTaskOutput::InheritedStdout?5u:4u)*sizeof(Native);
        ready=initialized&&UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,handleBytes,nullptr,nullptr);
        const auto flag=outputRole==DuplexTaskOutput::InheritedTerminal?L" --local-terminal-v1 ":outputRole==DuplexTaskOutput::InheritedStdout?L" --local-output-v1 ":L" --local-stream-v1 ";
        std::wstring command=L"\""+exe.wstring()+L"\""+flag+std::to_wstring(reinterpret_cast<uintptr_t>(child.input))+L" "+std::to_wstring(reinterpret_cast<uintptr_t>(child.output))+L" "+std::to_wstring(reinterpret_cast<uintptr_t>(child.parent));
        std::vector<wchar_t> words(command.begin(),command.end());words.push_back(0);wchar_t directory[32768]{};const auto n=GetWindowsDirectoryW(directory,32768);if(!n||n>=32768)ready=false;
        std::wstring environment=L"SystemRoot=";environment+=directory;environment.push_back(0);environment.push_back(0);const auto parent=exe.parent_path().wstring();
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdInput=startup.StartupInfo.hStdOutput=startup.StartupInfo.hStdError=child.null;startup.lpAttributeList=attributes;
        if(outputRole!=DuplexTaskOutput::Discard)startup.StartupInfo.hStdOutput=child.display;
        if(outputRole==DuplexTaskOutput::InheritedTerminal)startup.StartupInfo.hStdInput=child.keyboard;
        const DWORD flags=EXTENDED_STARTUPINFO_PRESENT|CREATE_UNICODE_ENVIRONMENT|CREATE_SUSPENDED|(console?CREATE_NEW_PROCESS_GROUP:CREATE_NO_WINDOW);
        PROCESS_INFORMATION pi{};const bool created=ready&&!cancel.load()&&admission.permitted()&&CreateProcessW(exe.c_str(),words.data(),nullptr,nullptr,TRUE,flags,environment.data(),parent.c_str(),&startup.StartupInfo,&pi);
        if(!created)return false;
        process=pi.hProcess;child.thread=pi.hThread;owned=true;jobBound=AssignProcessToJobObject(job,process)!=0;
        return jobBound&&!cancel.load()&&admission.permitted()&&ResumeThread(child.thread)!=static_cast<DWORD>(-1);
#else
        if(outputRole==DuplexTaskOutput::InheritedTerminal)return false; // Linux retains its existing nonblocking termios reader
        struct sigaction policy{};if(sigaction(SIGCHLD,nullptr,&policy)||policy.sa_handler!=SIG_DFL||(policy.sa_flags&SA_NOCLDWAIT))return false;
#if !defined(OM_DUPLEX_PIDFD_SPAWN) || !defined(SYS_pidfd_send_signal)
        return false;
#endif
        struct Pipes {int pair[2]={-1,-1},child=-1,display=-1;~Pipes(){for(int fd:{pair[0],pair[1],child,display})if(fd>=0)::close(fd);}} pipe;
        // Freeze before creating IPC descriptors; a closed stdout must never become the private socket.
        if(outputRole==DuplexTaskOutput::InheritedStdout){pipe.display=fcntl(STDOUT_FILENO,F_DUPFD_CLOEXEC,4);if(pipe.display<0)return false;}
        if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pipe.pair))return false;
        pipe.child=fcntl(pipe.pair[1],F_DUPFD_CLOEXEC,4);closeNative(pipe.pair[1]);channel.input=channel.output=pipe.pair[0];pipe.pair[0]=-1;
        if(pipe.child<0||!nonblocking(channel.input)||!nonblocking(pipe.child))return false;
        struct Setup {posix_spawn_file_actions_t actions;posix_spawnattr_t attributes;bool actionsReady=false,attributesReady=false;
            ~Setup(){if(attributesReady)posix_spawnattr_destroy(&attributes);if(actionsReady)posix_spawn_file_actions_destroy(&actions);}} setup;
        if(posix_spawn_file_actions_init(&setup.actions))return false;
        setup.actionsReady=true;
        if(posix_spawnattr_init(&setup.attributes))return false;
        setup.attributesReady=true;
        bool ready=posix_spawn_file_actions_addopen(&setup.actions,0,"/dev/null",O_RDONLY,0)==0&&
            (outputRole==DuplexTaskOutput::InheritedStdout?posix_spawn_file_actions_adddup2(&setup.actions,pipe.display,1):posix_spawn_file_actions_addopen(&setup.actions,1,"/dev/null",O_WRONLY,0))==0&&
            posix_spawn_file_actions_addopen(&setup.actions,2,"/dev/null",O_WRONLY,0)==0&&posix_spawn_file_actions_adddup2(&setup.actions,pipe.child,3)==0;
#if defined(OM_DUPLEX_PIDFD_SPAWN)
        ready=ready&&posix_spawn_file_actions_addclosefrom_np(&setup.actions,4)==0;
#endif
        sigset_t mask,defaults;sigemptyset(&mask);sigemptyset(&defaults);for(int sig:{SIGPIPE,SIGINT,SIGTERM})sigaddset(&defaults,sig);
        ready=ready&&posix_spawnattr_setflags(&setup.attributes,POSIX_SPAWN_SETPGROUP|POSIX_SPAWN_SETSIGMASK|POSIX_SPAWN_SETSIGDEF)==0&&
            posix_spawnattr_setpgroup(&setup.attributes,0)==0&&posix_spawnattr_setsigmask(&setup.attributes,&mask)==0&&posix_spawnattr_setsigdefault(&setup.attributes,&defaults)==0;
        auto executable=exe.string(),parent=std::to_string(getpid());char streamFlag[]="--local-stream-v1",outputFlag[]="--local-output-v1",fd[]="3",lang[]="LANG=C",locale[]="LC_ALL=C";
        char *flag=outputRole==DuplexTaskOutput::InheritedStdout?outputFlag:streamFlag;
        char *argv[]={executable.data(),flag,fd,fd,parent.data(),nullptr};char *environment[]={lang,locale,nullptr};
#if defined(OM_DUPLEX_PIDFD_SPAWN)
        const int result=ready&&!cancel.load()&&admission.permitted()?pidfd_spawn(&pidfd,executable.c_str(),&setup.actions,&setup.attributes,argv,environment):-1;
#else
        const int result=-1;(void)ready;(void)argv;(void)environment;(void)cancel;
#endif
        if(result){pidfd=-1;return false;}owned=true;return true;
#endif
    }
};
DuplexTaskProcess::DuplexTaskProcess():impl_(new Impl){}
DuplexTaskProcess::~DuplexTaskProcess(){if(!impl_->stop(250)){std::lock_guard<std::mutex> lock(Impl::mutex());Impl::quarantine().push_back(impl_.release());}
    else{std::lock_guard<std::mutex> lock(Impl::mutex());impl_.reset();}}
bool DuplexTaskProcess::start(const std::filesystem::path &exe,const std::atomic<bool> &cancel){return start(exe,cancel,DuplexTaskOutput::Discard);}
bool DuplexTaskProcess::start(const std::filesystem::path &exe,const std::atomic<bool> &cancel,DuplexTaskOutput outputRole){auto &p=*impl_;if(p.attempted)return false;p.attempted=true;
    if(outputRole!=DuplexTaskOutput::Discard&&outputRole!=DuplexTaskOutput::InheritedStdout&&outputRole!=DuplexTaskOutput::InheritedTerminal){p.state=S::Failed;return false;}
    if(cancel.load()){p.state=S::Stopped;return false;}
    try{if(!localTaskPath(exe)||!Impl::reserve(&p)){p.state=nativeProcessUnresolved()?S::Unreaped:S::Failed;return false;}p.reserved=true;
        if(!p.launch(exe,cancel,outputRole)||cancel.load()||!p.admission.permitted()){p.state=S::Failed;p.stop(250);return false;}p.state=S::Running;return true;}catch(...){p.state=S::Failed;p.stop(250);return false;}}
bool DuplexTaskProcess::send(const std::string &wire){auto &p=*impl_;if(p.state!=S::Running||!p.admission.permitted()||!p.pending.empty()||wire.empty()||wire.size()>duplexTaskFrameLimit)return false;p.pending=wire;p.offset=0;return true;}
DuplexTaskPoll DuplexTaskProcess::poll(){auto &p=*impl_;DuplexTaskPoll out;
    if(p.state==S::Running&&!p.admission.permitted()){p.fault=true;p.channel.close();p.state=S::Failed;}
    if(p.state==S::Running&&!p.pending.empty()){const auto sent=writeBytes(p.channel.output,p.pending.data()+p.offset,std::min<std::size_t>(4096,p.pending.size()-p.offset));
        if(sent.status==IO::Closed||sent.status==IO::Error||sent.bytes>p.pending.size()-p.offset){p.fault=true;p.state=S::Failed;}
        else{p.offset+=sent.bytes;if(p.offset==p.pending.size()){p.pending.clear();p.offset=0;}}}
    if((p.state==S::Running||p.state==S::Exited)&&!p.eof&&p.channel.input!=invalid){char bytes[4096];const auto got=readBytes(p.channel.input,bytes,sizeof(bytes));
        if(got.status==IO::Error){p.fault=true;p.state=S::Failed;}else if(got.status==IO::Closed)p.eof=true;else if(got.status==IO::Bytes)out.bytes.assign(bytes,got.bytes);}
    if(p.owned)p.observe();
    if(p.admission.invalidated()){p.fault=true;out.bytes.clear();p.channel.close();if(p.state!=S::Unreaped&&!p.reaped)p.state=S::Failed;}
    out.state=p.state;out.exitCode=p.exit;out.eof=p.eof;out.channelFault=p.fault;out.flushed=p.pending.empty();return out;
}
void DuplexTaskProcess::requestStop(){auto &p=*impl_;p.attempted=true;p.channel.close();if(p.state!=S::Exited&&p.state!=S::Unreaped)p.state=S::Stopped;}
bool DuplexTaskProcess::terminate(unsigned ms){requestStop();return impl_->stop(ms);}
struct DuplexTaskChannel::Impl {duplex_task_detail::Channel channel;bool attempted=false;DuplexTaskOutput role=DuplexTaskOutput::Discard;
#if defined(_WIN32)
    Native parent=invalid;~Impl(){closeNative(parent);}bool alive()const{return parent!=invalid&&WaitForSingleObject(parent,0)==WAIT_TIMEOUT;}
#else
    pid_t parent=-1;bool alive()const{return parent>0&&getppid()==parent;}
#endif
};
DuplexTaskChannel::DuplexTaskChannel():impl_(new Impl){}DuplexTaskChannel::~DuplexTaskChannel()=default;
bool DuplexTaskChannel::adopt(int argc,char **argv){return adopt(argc,argv,DuplexTaskOutput::Discard);}
bool DuplexTaskChannel::adopt(int argc,char **argv,DuplexTaskOutput outputRole){auto &p=*impl_;if(p.attempted)return false;p.attempted=true;uint64_t input=0,output=0,parent=0;
    if(outputRole!=DuplexTaskOutput::Discard&&outputRole!=DuplexTaskOutput::InheritedStdout&&outputRole!=DuplexTaskOutput::InheritedTerminal)return false;
    const auto flag=outputRole==DuplexTaskOutput::InheritedTerminal?"--local-terminal-v1":outputRole==DuplexTaskOutput::InheritedStdout?"--local-output-v1":"--local-stream-v1";
    if(argc!=5||std::string(argv[1])!=flag||!integer(argv[2],input)||!integer(argv[3],output)||!integer(argv[4],parent))return false;
#if defined(_WIN32)
    if(input>UINTPTR_MAX||output>UINTPTR_MAX||parent>UINTPTR_MAX||input==output||input==parent||output==parent)return false;
    auto r=reinterpret_cast<Native>(static_cast<uintptr_t>(input)),w=reinterpret_cast<Native>(static_cast<uintptr_t>(output)),h=reinterpret_cast<Native>(static_cast<uintptr_t>(parent));
    DWORD rp=0,wp=0,rs=0,ws=0;const auto pid=GetProcessId(h);BOOL job=FALSE;
    if(!pid||pid==GetCurrentProcessId()||WaitForSingleObject(h,0)!=WAIT_TIMEOUT||!GetNamedPipeServerProcessId(r,&rp)||!GetNamedPipeServerProcessId(w,&wp)||rp!=pid||wp!=pid||
        !GetNamedPipeHandleStateW(r,&rs,nullptr,nullptr,nullptr,nullptr,0)||!GetNamedPipeHandleStateW(w,&ws,nullptr,nullptr,nullptr,nullptr,0)||!(rs&PIPE_NOWAIT)||!(ws&PIPE_NOWAIT)||!IsProcessInJob(GetCurrentProcess(),nullptr,&job)||!job)return false;
    p.channel.input=r;p.channel.output=w;p.parent=h;if(!inherit(r,false)||!inherit(w,false)||!inherit(h,false))return false;
#else
    if(outputRole==DuplexTaskOutput::InheritedTerminal)return false;
    if(input!=3||output!=3||parent>UINT32_MAX||static_cast<uint64_t>(getppid())!=parent)return false;
    ucred peer{};socklen_t size=sizeof(peer);int type=0;socklen_t typeSize=sizeof(type);
    if(getsockopt(3,SOL_SOCKET,SO_PEERCRED,&peer,&size)||size!=sizeof(peer)||peer.pid!=getppid()||peer.uid!=getuid()||getsockopt(3,SOL_SOCKET,SO_TYPE,&type,&typeSize)||type!=SOCK_STREAM||
        prctl(PR_SET_PDEATHSIG,SIGKILL)||static_cast<uint64_t>(getppid())!=parent||fcntl(3,F_SETFD,FD_CLOEXEC)||!nonblocking(3))return false;
    p.channel.input=p.channel.output=3;p.parent=static_cast<pid_t>(parent);
#endif
    p.role=outputRole;return true;
}
DuplexTaskOutput DuplexTaskChannel::outputRole()const{return impl_->role;}
bool DuplexTaskChannel::parentAlive()const{return impl_->alive();}
uint64_t DuplexTaskChannel::parentId()const{
#if defined(_WIN32)
    return impl_->parent!=invalid?GetProcessId(impl_->parent):0;
#else
    return impl_->parent>0?static_cast<uint64_t>(impl_->parent):0;
#endif
}
DuplexTaskBytes DuplexTaskChannel::receive(char *p,std::size_t n){return p&&n&&n<=4096&&parentAlive()?readBytes(impl_->channel.input,p,n):B{};}
DuplexTaskBytes DuplexTaskChannel::send(const char *p,std::size_t n){return p&&n&&n<=4096&&parentAlive()?writeBytes(impl_->channel.output,p,n):B{};}
} // namespace om
