#include "duplex_task_process.h"
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
#include <unistd.h>
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
struct Channel {
    Native input=invalid,output=invalid;
    ~Channel(){close();}
    void close(){if(input==output)output=invalid;closeNative(input);closeNative(output);}
};
bool integer(const char *s,uint64_t &out){if(!s)return false;const std::string_view v(s);if(v.empty()||v.size()>20||(v.size()>1&&v[0]=='0')||v[0]<'0'||v[0]>'9')return false;
    const auto r=std::from_chars(v.data(),v.data()+v.size(),out);return r.ec==std::errc{}&&r.ptr==v.data()+v.size()&&out;}
}
struct DuplexTaskProcess::Impl {
    bool attempted=false,owned=false,reaped=false,unknown=false,reserved=false,eof=false,blocked=false,fault=false;
    S state=S::Dormant;int exit=-1;Channel channel;std::string pending;std::size_t offset=0;
#if defined(_WIN32)
    Native process=invalid,job=invalid;
#else
    pid_t pid=-1;
#endif
    static std::mutex &mutex(){static std::mutex value;return value;}
    static std::vector<Impl *> &quarantine(){static auto *value=new std::vector<Impl *>;return *value;}
    static unsigned &reservations(){static unsigned value=0;return value;}
    static std::atomic<unsigned> &unresolved(){static std::atomic<unsigned> value{0};return value;}
    void unreaped(){state=S::Unreaped;if(!blocked){blocked=true;++unresolved();}}
    ~Impl(){channel.close();
#if defined(_WIN32)
        closeNative(process);closeNative(job);
#endif
        if(reserved)--reservations(); // registry lock belongs to the deleting owner
    }
    bool observe(){if(!owned||reaped)return reaped;if(unknown)return false;
#if defined(_WIN32)
        const auto result=WaitForSingleObject(process,0);if(result==WAIT_TIMEOUT)return false;DWORD code=0;
        if(result!=WAIT_OBJECT_0||!GetExitCodeProcess(process,&code)){unknown=true;unreaped();return false;}exit=static_cast<int>(code);
#else
        int status=0;const auto result=waitpid(pid,&status,WNOHANG);if(result==0||(result<0&&errno==EINTR))return false;
        if(result!=pid){unknown=true;unreaped();return false;}exit=WIFEXITED(status)?WEXITSTATUS(status):128+(WIFSIGNALED(status)?WTERMSIG(status):0);
#endif
        reaped=true;if(blocked){blocked=false;--unresolved();}state=S::Exited;return true;
    }
    static bool reserve(){std::lock_guard<std::mutex> lock(mutex());auto &held=quarantine();
        if(held.capacity()<64)held.reserve(64); // allocate quarantine slots before any process launch, never while tearing one down
        for(auto i=held.begin();i!=held.end();){if((*i)->observe()){delete *i;i=held.erase(i);}else ++i;}
        if(!held.empty()||unresolved().load()||reservations()>=64)return false;
        ++reservations();return true;}
    bool stop(unsigned ms){channel.close();if(!owned)return true;if(observe())return true;if(unknown){unreaped();return false;}
#if defined(_WIN32)
        if(job!=invalid)TerminateJobObject(job,1);else TerminateProcess(process,1);
#else
        ::kill(-pid,SIGKILL);
#endif
        const auto end=Clock::now()+std::chrono::milliseconds(std::min(ms,2000u));
        do{if(observe())return true;if(Clock::now()>=end)break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}while(true);
        unreaped();return false;
    }
    bool launch(const std::filesystem::path &exe,const std::atomic<bool> &cancel){
#if defined(_WIN32)
        struct Temps {Native input=invalid,output=invalid,parent=invalid,null=invalid;~Temps(){closeNative(input);closeNative(output);closeNative(parent);closeNative(null);}} child;
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
        if(!CreatePipe(&child.input,&channel.output,&sa,8192)||!CreatePipe(&channel.input,&child.output,&sa,8192))return false;
        bool ready=inherit(channel.input,false)&&inherit(channel.output,false)&&nonblocking(channel.input)&&nonblocking(channel.output)&&nonblocking(child.input)&&nonblocking(child.output);
        child.null=CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
        ready=ready&&child.null!=invalid&&DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&child.parent,SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,TRUE,0);
        job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION policy{};policy.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_ACTIVE_PROCESS;policy.BasicLimitInformation.ActiveProcessLimit=1;
        ready=ready&&job&&SetInformationJobObject(job,JobObjectExtendedLimitInformation,&policy,sizeof(policy));
        SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);std::vector<unsigned char> storage(size);auto *attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        const bool initialized=ready&&InitializeProcThreadAttributeList(attributes,1,0,&size);
        Native handles[]={child.input,child.output,child.parent,child.null};ready=initialized&&UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr);
        std::wstring command=L"\""+exe.wstring()+L"\" --local-stream-v1 "+std::to_wstring(reinterpret_cast<uintptr_t>(child.input))+L" "+std::to_wstring(reinterpret_cast<uintptr_t>(child.output))+L" "+std::to_wstring(reinterpret_cast<uintptr_t>(child.parent));
        std::vector<wchar_t> words(command.begin(),command.end());words.push_back(0);wchar_t directory[32768]{};const auto n=GetWindowsDirectoryW(directory,32768);if(!n||n>=32768)ready=false;
        std::wstring environment=L"SystemRoot=";environment+=directory;environment.push_back(0);environment.push_back(0);const auto parent=exe.parent_path().wstring();
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdInput=startup.StartupInfo.hStdOutput=startup.StartupInfo.hStdError=child.null;startup.lpAttributeList=attributes;
        PROCESS_INFORMATION pi{};const bool created=ready&&!cancel.load()&&CreateProcessW(exe.c_str(),words.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_UNICODE_ENVIRONMENT|CREATE_NO_WINDOW|CREATE_SUSPENDED,environment.data(),parent.c_str(),&startup.StartupInfo,&pi);
        if(initialized)DeleteProcThreadAttributeList(attributes);if(!created)return false;
        process=pi.hProcess;owned=true;const bool bound=AssignProcessToJobObject(job,process)&&!cancel.load()&&ResumeThread(pi.hThread)!=static_cast<DWORD>(-1);CloseHandle(pi.hThread);
        if(!bound){TerminateProcess(process,1);stop(250);return false;}return true;
#else
        struct sigaction policy{};if(sigaction(SIGCHLD,nullptr,&policy)||policy.sa_handler!=SIG_DFL||(policy.sa_flags&SA_NOCLDWAIT))return false;
        int pair[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair))return false;
        const int child=fcntl(pair[1],F_DUPFD_CLOEXEC,4);::close(pair[1]);channel.input=channel.output=pair[0];
        if(child<0||!nonblocking(channel.input)||!nonblocking(child)){if(child>=0)::close(child);return false;}
        posix_spawn_file_actions_t actions;posix_spawnattr_t attributes;
        if(posix_spawn_file_actions_init(&actions)){::close(child);return false;}
        if(posix_spawnattr_init(&attributes)){posix_spawn_file_actions_destroy(&actions);::close(child);return false;}
        bool ready=posix_spawn_file_actions_addopen(&actions,0,"/dev/null",O_RDONLY,0)==0&&posix_spawn_file_actions_addopen(&actions,1,"/dev/null",O_WRONLY,0)==0&&
            posix_spawn_file_actions_addopen(&actions,2,"/dev/null",O_WRONLY,0)==0&&posix_spawn_file_actions_adddup2(&actions,child,3)==0;
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
        auto executable=exe.string(),parent=std::to_string(getpid());char flag[]="--local-stream-v1",fd[]="3",lang[]="LANG=C",locale[]="LC_ALL=C";
        char *argv[]={executable.data(),flag,fd,fd,parent.data(),nullptr};char *environment[]={lang,locale,nullptr};
        const int result=ready&&!cancel.load()?posix_spawn(&pid,executable.c_str(),&actions,&attributes,argv,environment):-1;
        posix_spawn_file_actions_destroy(&actions);posix_spawnattr_destroy(&attributes);::close(child);if(result){pid=-1;return false;}owned=true;return true;
#endif
    }
};
DuplexTaskProcess::DuplexTaskProcess():impl_(new Impl){}
DuplexTaskProcess::~DuplexTaskProcess(){if(!impl_->stop(250)){std::lock_guard<std::mutex> lock(Impl::mutex());Impl::quarantine().push_back(impl_.release());}
    else{std::lock_guard<std::mutex> lock(Impl::mutex());impl_.reset();}}
bool DuplexTaskProcess::start(const std::filesystem::path &exe,const std::atomic<bool> &cancel){auto &p=*impl_;if(p.attempted)return false;p.attempted=true;
    if(cancel.load()){p.state=S::Stopped;return false;}
    try{if(!localTaskPath(exe)||!Impl::reserve()){p.state=S::Failed;return false;}p.reserved=true;
        if(!p.launch(exe,cancel)||cancel.load()){p.state=S::Failed;p.stop(250);return false;}p.state=S::Running;return true;}catch(...){p.state=S::Failed;p.stop(250);return false;}}
bool DuplexTaskProcess::send(const std::string &wire){auto &p=*impl_;if(p.state!=S::Running||!p.pending.empty()||wire.empty()||wire.size()>duplexTaskFrameLimit)return false;p.pending=wire;p.offset=0;return true;}
DuplexTaskPoll DuplexTaskProcess::poll(){auto &p=*impl_;DuplexTaskPoll out;
    if(p.state==S::Running&&!p.pending.empty()){const auto sent=writeBytes(p.channel.output,p.pending.data()+p.offset,std::min<std::size_t>(4096,p.pending.size()-p.offset));
        if(sent.status==IO::Closed||sent.status==IO::Error||sent.bytes>p.pending.size()-p.offset){p.fault=true;p.state=S::Failed;}
        else{p.offset+=sent.bytes;if(p.offset==p.pending.size()){p.pending.clear();p.offset=0;}}}
    if((p.state==S::Running||p.state==S::Exited)&&!p.eof&&p.channel.input!=invalid){char bytes[4096];const auto got=readBytes(p.channel.input,bytes,sizeof(bytes));
        if(got.status==IO::Error){p.fault=true;p.state=S::Failed;}else if(got.status==IO::Closed)p.eof=true;else if(got.status==IO::Bytes)out.bytes.assign(bytes,got.bytes);}
    if(p.owned)p.observe();
    out.state=p.state;out.exitCode=p.exit;out.eof=p.eof;out.channelFault=p.fault;out.flushed=p.pending.empty();return out;
}
void DuplexTaskProcess::requestStop(){auto &p=*impl_;p.attempted=true;p.channel.close();if(p.state!=S::Exited&&p.state!=S::Unreaped)p.state=S::Stopped;}
bool DuplexTaskProcess::terminate(unsigned ms){requestStop();return impl_->stop(ms);}
struct DuplexTaskChannel::Impl {Channel channel;bool attempted=false;
#if defined(_WIN32)
    Native parent=invalid;~Impl(){closeNative(parent);}bool alive()const{return parent!=invalid&&WaitForSingleObject(parent,0)==WAIT_TIMEOUT;}
#else
    pid_t parent=-1;bool alive()const{return parent>0&&getppid()==parent;}
#endif
};
DuplexTaskChannel::DuplexTaskChannel():impl_(new Impl){}DuplexTaskChannel::~DuplexTaskChannel()=default;
bool DuplexTaskChannel::adopt(int argc,char **argv){auto &p=*impl_;if(p.attempted)return false;p.attempted=true;uint64_t input=0,output=0,parent=0;
    if(argc!=5||std::string(argv[1])!="--local-stream-v1"||!integer(argv[2],input)||!integer(argv[3],output)||!integer(argv[4],parent))return false;
#if defined(_WIN32)
    if(input>UINTPTR_MAX||output>UINTPTR_MAX||parent>UINTPTR_MAX||input==output||input==parent||output==parent)return false;
    auto r=reinterpret_cast<Native>(static_cast<uintptr_t>(input)),w=reinterpret_cast<Native>(static_cast<uintptr_t>(output)),h=reinterpret_cast<Native>(static_cast<uintptr_t>(parent));
    DWORD rp=0,wp=0,rs=0,ws=0;const auto pid=GetProcessId(h);BOOL job=FALSE;
    if(!pid||pid==GetCurrentProcessId()||WaitForSingleObject(h,0)!=WAIT_TIMEOUT||!GetNamedPipeServerProcessId(r,&rp)||!GetNamedPipeServerProcessId(w,&wp)||rp!=pid||wp!=pid||
        !GetNamedPipeHandleStateW(r,&rs,nullptr,nullptr,nullptr,nullptr,0)||!GetNamedPipeHandleStateW(w,&ws,nullptr,nullptr,nullptr,nullptr,0)||!(rs&PIPE_NOWAIT)||!(ws&PIPE_NOWAIT)||!IsProcessInJob(GetCurrentProcess(),nullptr,&job)||!job)return false;
    p.channel.input=r;p.channel.output=w;p.parent=h;if(!inherit(r,false)||!inherit(w,false)||!inherit(h,false))return false;
#else
    if(input!=3||output!=3||parent>UINT32_MAX||static_cast<uint64_t>(getppid())!=parent)return false;
    ucred peer{};socklen_t size=sizeof(peer);int type=0;socklen_t typeSize=sizeof(type);
    if(getsockopt(3,SOL_SOCKET,SO_PEERCRED,&peer,&size)||size!=sizeof(peer)||peer.pid!=getppid()||peer.uid!=getuid()||getsockopt(3,SOL_SOCKET,SO_TYPE,&type,&typeSize)||type!=SOCK_STREAM||
        prctl(PR_SET_PDEATHSIG,SIGKILL)||static_cast<uint64_t>(getppid())!=parent||fcntl(3,F_SETFD,FD_CLOEXEC)||!nonblocking(3))return false;
    p.channel.input=p.channel.output=3;p.parent=static_cast<pid_t>(parent);
#endif
    return true;
}
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
