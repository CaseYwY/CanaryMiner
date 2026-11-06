// Ordinary supervisor child: creation-bound native identity and retained uncertainty, not a controlled-work permit.
#pragma once
#include "../core/platform.h" // winsock2.h must precede windows.h
#include "native_process_admission.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2,40)
#define OM_LEGACY_PIDFD_SPAWN 1
#endif
#endif
#endif

namespace om {
/** Splits LF/CRLF bytes; cuts long unterminated output at 8 KiB. */
inline void feedLines(std::string *pending,const char *data,size_t n,const std::function<void(const std::string &)> &sink){
    pending->append(data,n);size_t start=0,nl;
    while((nl=pending->find('\n',start))!=std::string::npos){
        std::string line=pending->substr(start,nl-start);if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(!line.empty())sink(line);
        start=nl+1;
    }
    pending->erase(0,start);
    if(pending->size()>8192){sink(pending->substr(0,8192));pending->clear();}
}

namespace legacy_child_detail {
using Clock=std::chrono::steady_clock;
#if defined(_WIN32)
using Native=HANDLE;
inline const Native invalid=INVALID_HANDLE_VALUE;
inline void closeNative(Native &handle){if(handle&&handle!=invalid)CloseHandle(handle);handle=invalid;}
inline std::wstring quote(const std::wstring &value){
    std::wstring result=L"\"";size_t slashes=0;
    for(const wchar_t c:value){if(c==L'\\'){++slashes;continue;}
        result.append(slashes*(c==L'"'?2:1)+(c==L'"'?1:0),L'\\');slashes=0;result+=c;}
    result.append(slashes*2,L'\\');return result+L'"';
}
#else
using Native=int;
inline constexpr Native invalid=-1;
inline void closeNative(Native &fd){if(fd>=0)::close(fd);fd=-1;}
#endif
struct Owner {
    bool reserved=false,created=false,done=false,unknown=false,verified=false,readerFailureReported=false;
    int code=-1;
    Native input=invalid,output=invalid;
    NativeProcessAdmission admission;
    std::function<void(const std::string &)> sink;
    std::atomic<bool> readerStop{false},readerDrain{false},readerFailed{false};
    std::thread reader;
#if defined(_WIN32)
    Native process=invalid,job=invalid;bool jobBound=false;
#else
    int pidfd=-1;
#endif
    static std::mutex &registryMutex(){static std::mutex value;return value;}
    static unsigned &reservations(){static unsigned value=0;return value;}
    static std::array<Owner *,256> &quarantine(){static std::array<Owner *,256> value{};return value;}
    ~Owner(){closeNative(input);closeNative(output);
#if defined(_WIN32)
        closeNative(process);closeNative(job);
#else
        closeNative(pidfd);
#endif
        if(reserved)--reservations(); // destruction is serialized by registryMutex
    }
    void noticeReaderFault(){
        if(readerFailed.load()&&!readerFailureReported){readerFailureReported=true;admission.markUnresolved();
            closeNative(input);if(done||!created)admission.settled();}
    }
    bool observe(){
        noticeReaderFault();
        if(!created||done)return done;
        if(unknown)return false; // losing native reaping ownership is terminal; never probe or signal again
        verified=false;
#if defined(_WIN32)
        const auto result=WaitForSingleObject(process,0);
        if(result==WAIT_TIMEOUT){verified=true;return false;}
        DWORD status=0;
        if(result!=WAIT_OBJECT_0||!GetExitCodeProcess(process,&status)){unknown=true;admission.markUnresolved();return false;}
        code=static_cast<int>(status);
#else
#if defined(OM_LEGACY_PIDFD_SPAWN)
        siginfo_t status{};const int result=waitid(P_PIDFD,static_cast<id_t>(pidfd),&status,WEXITED|WNOHANG);
        if(result<0&&errno==EINTR)return false;
        if(result||(status.si_pid&&status.si_code!=CLD_EXITED&&status.si_code!=CLD_KILLED&&status.si_code!=CLD_DUMPED)){
            unknown=true;admission.markUnresolved();return false;}
        verified=true;if(!status.si_pid)return false;
        code=status.si_code==CLD_EXITED?status.si_status:128+status.si_status;
#else
        unknown=true;admission.markUnresolved();return false;
#endif
#endif
        done=true;admission.settled();closeNative(input);readerDrain=true;return true;
    }
    static bool reserve(Owner &owner){
        std::lock_guard<std::mutex> lock(registryMutex());
        for(auto &held:quarantine())if(held&&held->observe()){delete held;held=nullptr;}
        if(reservations()>=quarantine().size()||!owner.admission.begin())return false;
        ++reservations();owner.reserved=true;return true;
    }
    bool terminate(unsigned ms){
        closeNative(input);if(!created||observe())return true;
        if(unknown)return false;
        const auto until=Clock::now()+std::chrono::milliseconds(ms);bool signalled=false;
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
            if(unknown||Clock::now()>=until)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }while(true);
        admission.markUnresolved();return false;
    }
    void readLoop()noexcept{
        try{
            std::string pending;char bytes[4096];
            const auto deliver=[this](const std::string &line){if(!readerStop.load()&&admission.unchanged()&&sink)sink(line);};
            while(!readerStop.load()&&admission.unchanged()){
                size_t count=0;bool closed=false,failed=false;
#if defined(_WIN32)
                DWORD received=0;const BOOL ok=ReadFile(output,bytes,sizeof(bytes),&received,nullptr);
                count=received;
                if(!ok){const auto error=GetLastError();closed=error==ERROR_BROKEN_PIPE;failed=!closed&&error!=ERROR_NO_DATA;}
                else closed=!count;
#else
                const auto received=::read(output,bytes,sizeof(bytes));
                if(received>0)count=static_cast<size_t>(received);
                else if(!received)closed=true;
                else failed=errno!=EINTR&&errno!=EAGAIN&&errno!=EWOULDBLOCK;
#endif
                if(count)feedLines(&pending,bytes,count,deliver);
                if(failed){readerFailed=true;break;}
                if(closed||(!count&&readerDrain.load()))break;
                if(!count)std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if(!pending.empty())deliver(pending);
        }catch(...){readerFailed=true;}
        closeNative(output);
    }
    void joinReader(){
        if(done)readerDrain=true;else readerStop=true;
        if(reader.joinable())reader.join();
        closeNative(output);sink={};noticeReaderFault();
    }
    bool allowed(const std::function<bool()> &stopped)const{
        return admission.permitted()&&(!stopped||!stopped())&&admission.permitted();
    }
    bool launch(const std::filesystem::path &exe,const std::vector<std::string> &args,const std::function<bool()> &stopped){
#if defined(_WIN32)
        struct Temps {Native childIn=invalid,childOut=invalid,thread=invalid;
            ~Temps(){closeNative(childIn);closeNative(childOut);closeNative(thread);}} temp;
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
        if(!CreatePipe(&temp.childIn,&input,&security,4096)||!CreatePipe(&output,&temp.childOut,&security,8192))return false;
        DWORD mode=PIPE_READMODE_BYTE|PIPE_NOWAIT;
        bool ready=SetHandleInformation(input,HANDLE_FLAG_INHERIT,0)&&SetHandleInformation(output,HANDLE_FLAG_INHERIT,0)&&
            SetNamedPipeHandleState(output,&mode,nullptr,nullptr);
        job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION policy{};
        policy.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        policy.BasicLimitInformation.ActiveProcessLimit=1;
        ready=ready&&job&&SetInformationJobObject(job,JobObjectExtendedLimitInformation,&policy,sizeof(policy));
        SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);std::vector<unsigned char> buffer(size);
        auto *attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
        const bool initialized=ready&&InitializeProcThreadAttributeList(attributes,1,0,&size);
        struct Attributes {LPPROC_THREAD_ATTRIBUTE_LIST value;~Attributes(){if(value)DeleteProcThreadAttributeList(value);}} cleanup{initialized?attributes:nullptr};
        HANDLE handles[]={temp.childIn,temp.childOut};
        ready=initialized&&UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr);
        std::wstring command=quote(exe.wstring());for(const auto &arg:args)command+=L" "+quote(std::filesystem::u8path(arg).wstring());
        if(command.size()>=32767)return false;
        std::vector<wchar_t> mutableCommand(command.begin(),command.end());mutableCommand.push_back(0);
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput=temp.childIn;startup.StartupInfo.hStdOutput=startup.StartupInfo.hStdError=temp.childOut;startup.lpAttributeList=attributes;
        PROCESS_INFORMATION info{};
        if(!ready||!allowed(stopped)||!CreateProcessW(exe.c_str(),mutableCommand.data(),nullptr,nullptr,TRUE,
            EXTENDED_STARTUPINFO_PRESENT|CREATE_SUSPENDED,nullptr,nullptr,&startup.StartupInfo,&info))return false;
        created=true;process=info.hProcess;temp.thread=info.hThread;
        jobBound=AssignProcessToJobObject(job,process)!=0;
        // Preserve the parent's console; no detached/new-console/new-process-group flag is introduced.
        return jobBound&&allowed(stopped)&&ResumeThread(temp.thread)!=static_cast<DWORD>(-1);
#else
        struct sigaction original{};
        if(sigaction(SIGCHLD,nullptr,&original)!=0||original.sa_handler!=SIG_DFL||(original.sa_flags&SA_NOCLDWAIT))return false;
#if !defined(OM_LEGACY_PIDFD_SPAWN) || !defined(SYS_pidfd_send_signal)
        (void)exe;(void)args;(void)stopped;return false;
#else
        struct Pipes {int in[2]={-1,-1},out[2]={-1,-1},childIn=-1,childOut=-1;
            ~Pipes(){for(int fd:{in[0],in[1],out[0],out[1],childIn,childOut})if(fd>=0)::close(fd);}} pipe;
        if(pipe2(pipe.in,O_CLOEXEC)!=0||pipe2(pipe.out,O_CLOEXEC)!=0)return false;
        pipe.childIn=fcntl(pipe.in[0],F_DUPFD_CLOEXEC,3);pipe.childOut=fcntl(pipe.out[1],F_DUPFD_CLOEXEC,3);
        closeNative(pipe.in[0]);closeNative(pipe.out[1]);input=pipe.in[1];pipe.in[1]=-1;output=pipe.out[0];pipe.out[0]=-1;
        const int flags=fcntl(output,F_GETFL);
        bool ready=pipe.childIn>=0&&pipe.childOut>=0&&flags>=0&&fcntl(output,F_SETFL,flags|O_NONBLOCK)==0;
        struct Setup {posix_spawn_file_actions_t actions;posix_spawnattr_t attributes;bool actionsReady=false,attributesReady=false;
            ~Setup(){if(attributesReady)posix_spawnattr_destroy(&attributes);if(actionsReady)posix_spawn_file_actions_destroy(&actions);}} setup;
        if(posix_spawn_file_actions_init(&setup.actions)!=0)return false;
        setup.actionsReady=true;
        if(posix_spawnattr_init(&setup.attributes)!=0)return false;
        setup.attributesReady=true;
        ready=ready&&posix_spawn_file_actions_adddup2(&setup.actions,pipe.childIn,0)==0&&
            posix_spawn_file_actions_adddup2(&setup.actions,pipe.childOut,1)==0&&posix_spawn_file_actions_adddup2(&setup.actions,pipe.childOut,2)==0&&
            posix_spawn_file_actions_addclosefrom_np(&setup.actions,3)==0;
        sigset_t mask,defaults;sigemptyset(&mask);sigemptyset(&defaults);for(int signal:{SIGPIPE,SIGINT,SIGTERM})sigaddset(&defaults,signal);
        // The inherited console process group remains unchanged, so ordinary Ctrl+C still reaches the child.
        ready=ready&&posix_spawnattr_setflags(&setup.attributes,POSIX_SPAWN_SETSIGMASK|POSIX_SPAWN_SETSIGDEF)==0&&
            posix_spawnattr_setsigmask(&setup.attributes,&mask)==0&&posix_spawnattr_setsigdefault(&setup.attributes,&defaults)==0;
        const auto executable=exe.string();std::vector<char *> argv;argv.push_back(const_cast<char *>(executable.c_str()));
        for(const auto &arg:args)argv.push_back(const_cast<char *>(arg.c_str()));
        argv.push_back(nullptr);
        const int result=ready&&allowed(stopped)?pidfd_spawn(&pidfd,executable.c_str(),&setup.actions,&setup.attributes,argv.data(),environ):-1;
        created=result==0;return created&&allowed(stopped);
#endif
#endif
    }
};
inline bool validPath(const std::filesystem::path &path){
    if(!path.is_absolute()||path.native().empty()||path.native().size()>4096||path.native().find(static_cast<std::filesystem::path::value_type>(0))!=path.native().npos)return false;
#if defined(_WIN32)
    const auto root=path.root_name().native();
    if(root.size()!=2||root[1]!=L':'||path.native().find(L'"')!=path.native().npos||path.native().find(L':',2)!=path.native().npos)return false;
#endif
    return true;
}
} // namespace legacy_child_detail

/** Single-use ordinary child. Closing stdin requests graceful drain; console signal propagation is preserved. */
class ChildProcess {
public:
    using LineSink=std::function<void(const std::string &)>;
    ChildProcess()=default;
    ChildProcess(const ChildProcess &)=delete;
    ChildProcess &operator=(const ChildProcess &)=delete;
    ~ChildProcess(){
        if(!owner_)return;
        owner_->terminate(250);owner_->joinReader();
        std::lock_guard<std::mutex> lock(legacy_child_detail::Owner::registryMutex());
        if(owner_->created&&!owner_->done){
            owner_->admission.markUnresolved();
            for(auto &held:legacy_child_detail::Owner::quarantine())if(!held){held=owner_.release();break;}
        }else{owner_->admission.settled();owner_.reset();}
    }
    bool start(const std::string &exe,const std::vector<std::string> &args,LineSink sink,std::string *error,const std::function<bool()> &stopped={}){
        if(attempted_)return refuse(error);
        attempted_=true;
        try{
            if(stopped&&stopped())return refuse(error);
            const auto path=std::filesystem::u8path(exe);if(!legacy_child_detail::validPath(path)||args.size()>256)return refuse(error);
            size_t bytes=0;for(const auto &arg:args){if(arg.size()>4096||arg.find('\0')!=arg.npos)return refuse(error);bytes+=arg.size();}
            if(bytes>24000)return refuse(error);
            owner_=std::make_unique<legacy_child_detail::Owner>();owner_->sink=std::move(sink);
            if(!legacy_child_detail::Owner::reserve(*owner_)||!owner_->launch(path,args,stopped)){
                owner_->terminate(250);return refuse(error);}
            owner_->reader=std::thread([owner=owner_.get()]{owner->readLoop();});
            if(!owner_->allowed(stopped)){owner_->terminate(250);owner_->joinReader();return refuse(error);}
            return true;
        }catch(...){if(owner_){owner_->terminate(250);owner_->joinReader();}return refuse(error);}
    }
    bool started()const{return owner_&&owner_->created;}
    bool finished()const{return owner_&&owner_->done;}
    bool exited(int *code){if(!owner_||!owner_->observe())return false;if(code)*code=owner_->code;return true;}
    void closeStdin(){if(owner_)legacy_child_detail::closeNative(owner_->input);}
    bool waitFor(uint64_t ms,int *code){
        if(!started())return false;
        const auto begin=legacy_child_detail::Clock::now();
        do{if(exited(code))return true;if(owner_->unknown||
            std::chrono::duration_cast<std::chrono::milliseconds>(legacy_child_detail::Clock::now()-begin).count()>=static_cast<int64_t>((std::min)(ms,static_cast<uint64_t>(INT64_MAX))))return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));}while(true);
    }
    void kill(){if(owner_)owner_->terminate(250);}
    void joinReader(){if(owner_)owner_->joinReader();}
private:
    static bool refuse(std::string *error){if(error)*error="Worker creation or safe native ownership unavailable.";return false;}
    bool attempted_=false;
    std::unique_ptr<legacy_child_detail::Owner> owner_;
};
} // namespace om
