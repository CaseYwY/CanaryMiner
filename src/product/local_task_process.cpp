#include "local_task_process.h"
#include "native_process_admission.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#include <sys/syscall.h>
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2,40)
#define OM_LOCAL_PIDFD_SPAWN 1
#endif
#endif
#endif
#endif

namespace om {
uint64_t localTaskProcessId(){
#if defined(_WIN32)
    return GetCurrentProcessId();
#else
    return static_cast<uint64_t>(getpid());
#endif
}
bool bindLocalTaskParent(uint64_t expected){
    if(!expected||expected>UINT32_MAX)return false;
#if defined(_WIN32)
    // LocalTaskProcess assigns the suspended child to its non-inherited KILL_ON_JOB_CLOSE job before resuming it.
    BOOL owned=FALSE;return IsProcessInJob(GetCurrentProcess(),nullptr,&owned)&&owned;
#elif defined(__linux__)
    if(static_cast<uint64_t>(getppid())!=expected||prctl(PR_SET_PDEATHSIG,SIGKILL)!=0)return false;
    return static_cast<uint64_t>(getppid())==expected; // close the death-before-prctl race
#else
    return false;
#endif
}
bool localTaskPath(const std::filesystem::path &path) {
    if (!path.is_absolute() || path.native().empty() || path.native().size() > 4096 || path.native().find(static_cast<std::filesystem::path::value_type>(0)) != std::filesystem::path::string_type::npos) return false;
#if defined(_WIN32)
    const auto root = path.root_name().native();
    if (root.size() != 2 || root[1] != L':' || path.native().find(L'"') != std::wstring::npos || path.native().find(L':',2) != std::wstring::npos) return false;
#endif
    return true;
}
struct LocalTaskProcess::Impl {
    explicit Impl(std::filesystem::path path) : helper(std::move(path)) {}
    std::filesystem::path helper;
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
    NativeProcessAdmission admission;
    bool reaped=false,success=false,unknown=false,verified=false,blocked=false,reserved=false,created=false;
    static std::mutex &mutex(){static std::mutex value;return value;}
    static std::vector<Impl *> &quarantine(){static auto *value=new std::vector<Impl *>;return *value;}
    static unsigned &reservations(){static unsigned value=0;return value;}
    static std::atomic<unsigned> &unresolved(){static std::atomic<unsigned> value{0};return value;}
    void unreaped(){if(!blocked){blocked=true;++unresolved();admission.markUnresolved();}}
    void settled(){if(blocked){blocked=false;--unresolved();}admission.settled();}
    static LocalTaskStatus reserve(Impl *p){
        std::lock_guard<std::mutex> lock(mutex());auto &held=quarantine();
        if(held.capacity()<64)held.reserve(64); // retain without allocating during teardown
        for(auto i=held.begin();i!=held.end();){bool okay=false;
            if((*i)->exited(okay)){(*i)->close();delete *i;i=held.erase(i);}else ++i;}
        if(!held.empty()||unresolved().load())return LocalTaskStatus::Unreaped;
        if(reservations()>=64)return LocalTaskStatus::Busy;
        if(!p->admission.begin())return LocalTaskStatus::Unreaped;
        ++reservations();p->reserved=true;return LocalTaskStatus::Ok;
    }
    void release(){std::lock_guard<std::mutex> lock(mutex());if(reserved){reserved=false;--reservations();}admission.settled();}
#if defined(_WIN32)
    HANDLE process = nullptr, output = nullptr, job = nullptr;
    bool jobBound=false;
    static void closeHandle(HANDLE &h){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=nullptr;}
    bool owned()const{return process!=nullptr;}
    void closeChannels(){closeHandle(output);}
    void close(){closeChannels();closeHandle(process);closeHandle(job);jobBound=false;}
    bool start(const std::string &request,const std::atomic<bool> &cancel) {
        struct Temps {
            HANDLE read=nullptr,write=nullptr,section=nullptr,inherited=nullptr,null=nullptr,thread=nullptr;
            ~Temps(){closeHandle(read);closeHandle(write);closeHandle(section);closeHandle(inherited);closeHandle(null);closeHandle(thread);}
        } temp;
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
        if (!CreatePipe(&temp.read,&temp.write,&sa,0)) return false;
        bool ready = SetHandleInformation(temp.read,HANDLE_FLAG_INHERIT,0) != 0;
        temp.section = CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,static_cast<DWORD>(request.size()),nullptr);
        void *view = temp.section ? MapViewOfFile(temp.section,FILE_MAP_WRITE,0,0,request.size()) : nullptr;
        if (view) { std::memcpy(view,request.data(),request.size()); UnmapViewOfFile(view); }
        ready = ready && view && DuplicateHandle(GetCurrentProcess(),temp.section,GetCurrentProcess(),&temp.inherited,FILE_MAP_READ,TRUE,0);
        closeHandle(temp.section);
        temp.null = CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
        job = CreateJobObjectW(nullptr,nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION policy{};
        policy.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        policy.BasicLimitInformation.ActiveProcessLimit = 1;
        ready = ready && temp.null != INVALID_HANDLE_VALUE && job && SetInformationJobObject(job,JobObjectExtendedLimitInformation,&policy,sizeof(policy));
        SIZE_T bytes = 0; InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
        std::vector<unsigned char> buffer(bytes);
        auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
        const bool initialized = ready && InitializeProcThreadAttributeList(attributes,1,0,&bytes);
        struct AttributeCleanup {LPPROC_THREAD_ATTRIBUTE_LIST value;~AttributeCleanup(){if(value)DeleteProcThreadAttributeList(value);}} cleanup{initialized?attributes:nullptr};
        HANDLE inherit[] = {temp.write,temp.null,temp.inherited};
        ready = initialized && UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherit,sizeof(inherit),nullptr,nullptr);
        STARTUPINFOEXW si{}; si.StartupInfo.cb = sizeof(si); si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = temp.null; si.StartupInfo.hStdOutput = temp.write; si.StartupInfo.hStdError = temp.null; si.lpAttributeList = attributes;
        // Request originals stay in an anonymous read-only mapping, never command arguments.
        const std::wstring command = L"\"" + helper.wstring() + L"\" --local-task-v1 " + std::to_wstring(reinterpret_cast<uintptr_t>(temp.inherited)) + L" " + std::to_wstring(request.size());
        std::vector<wchar_t> cmd(command.begin(),command.end()); cmd.push_back(0);
        wchar_t directory[32768] = {}; const DWORD length = GetWindowsDirectoryW(directory,32768);
        if (!length || length >= 32768) ready = false;
        std::wstring environment = L"SystemRoot="; environment += directory; environment.push_back(0); environment.push_back(0);
        PROCESS_INFORMATION pi{}; const auto parent = helper.parent_path().wstring();
        const bool spawned = ready && !cancel.load() && admission.permitted() && CreateProcessW(helper.c_str(),cmd.data(),nullptr,nullptr,TRUE,
            EXTENDED_STARTUPINFO_PRESENT|CREATE_UNICODE_ENVIRONMENT|CREATE_NO_WINDOW|CREATE_SUSPENDED,&environment[0],parent.c_str(),&si.StartupInfo,&pi);
        if(!spawned)return false;
        created=true;process=pi.hProcess;temp.thread=pi.hThread;output=temp.read;temp.read=nullptr;
        jobBound=AssignProcessToJobObject(job,process)!=0;
        return jobBound&&!cancel.load()&&admission.permitted()&&ResumeThread(temp.thread)!=static_cast<DWORD>(-1);
    }
    bool send(const std::string &) { return true; } // no potentially blocking parent pipe writes on Windows
    bool read(std::string &wire) {
        for (unsigned chunk = 0; chunk < 8; ++chunk) {
            DWORD available = 0;
            if (!PeekNamedPipe(output,nullptr,0,nullptr,&available,nullptr)) return GetLastError() == ERROR_BROKEN_PIPE;
            if (!available) return true;
            char data[4096]; DWORD count = 0;
            if (!ReadFile(output,data,(std::min)(available,static_cast<DWORD>(sizeof(data))),&count,nullptr) || count > localTaskResponseLimit-wire.size()) return false;
            wire.append(data,count);
        }
        return true;
    }
    bool exited(bool &ok) {
        if(reaped){ok=success;return true;}if(!owned()||unknown)return false;
        const auto result=WaitForSingleObject(process,0);if(result==WAIT_TIMEOUT){verified=true;return false;}
        DWORD code=1;if(result!=WAIT_OBJECT_0||!GetExitCodeProcess(process,&code)){unknown=true;unreaped();return false;}
        reaped=true;success=code==0;settled();ok=success;return true;
    }
    void terminateOwned(){if(jobBound)TerminateJobObject(job,1);else TerminateProcess(process,1);}
#else
    int input = -1, output = -1, pidfd = -1;
    size_t sent = 0;
    bool owned()const{return pidfd>=0;}
    void closeChannels(){
        if (input >= 0) { ::close(input); input = -1; }
        if (output >= 0) { ::close(output); output = -1; }
    }
    void close(){closeChannels();if(pidfd>=0){::close(pidfd);pidfd=-1;}sent=0;}
    bool start(const std::string &,const std::atomic<bool> &cancel) {
        struct sigaction policy{};
        if (sigaction(SIGCHLD,nullptr,&policy) != 0 || policy.sa_handler != SIG_DFL || (policy.sa_flags & SA_NOCLDWAIT)) return false;
#if !defined(OM_LOCAL_PIDFD_SPAWN) || !defined(SYS_pidfd_send_signal)
        return false;
#endif
        struct Pipes {int request[2]={-1,-1},reply[2]={-1,-1};~Pipes(){for(int fd:{request[0],request[1],reply[0],reply[1]})if(fd>=0)::close(fd);}} pipe;
        if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pipe.request)||pipe2(pipe.reply,O_CLOEXEC))return false;
        struct Setup {
            posix_spawn_file_actions_t actions;posix_spawnattr_t attributes;bool actionsReady=false,attributesReady=false;
            ~Setup(){if(attributesReady)posix_spawnattr_destroy(&attributes);if(actionsReady)posix_spawn_file_actions_destroy(&actions);}
        } setup;
        if(posix_spawn_file_actions_init(&setup.actions))return false;
        setup.actionsReady=true;
        if(posix_spawnattr_init(&setup.attributes))return false;
        setup.attributesReady=true;
        bool ready=fcntl(pipe.reply[0],F_SETFL,O_NONBLOCK)==0&&
            posix_spawn_file_actions_adddup2(&setup.actions,pipe.request[1],0)==0&&
            posix_spawn_file_actions_adddup2(&setup.actions,pipe.reply[1],1)==0&&
            posix_spawn_file_actions_addopen(&setup.actions,2,"/dev/null",O_WRONLY,0)==0;
#if defined(OM_LOCAL_PIDFD_SPAWN)
        ready=ready&&posix_spawn_file_actions_addclosefrom_np(&setup.actions,3)==0;
#endif
        sigset_t mask, defaults; sigemptyset(&mask); sigemptyset(&defaults); sigaddset(&defaults,SIGPIPE); sigaddset(&defaults,SIGINT); sigaddset(&defaults,SIGTERM);
        ready = ready && posix_spawnattr_setflags(&setup.attributes,POSIX_SPAWN_SETPGROUP|POSIX_SPAWN_SETSIGMASK|POSIX_SPAWN_SETSIGDEF) == 0 &&
            posix_spawnattr_setpgroup(&setup.attributes,0) == 0 && posix_spawnattr_setsigmask(&setup.attributes,&mask) == 0 && posix_spawnattr_setsigdefault(&setup.attributes,&defaults) == 0;
        const auto executable = helper.string(); char flag[] = "--local-task-v1";
        char *argv[] = {const_cast<char *>(executable.c_str()),flag,nullptr};
        char lang[] = "LANG=C", locale[] = "LC_ALL=C"; char *environment[] = {lang,locale,nullptr};
#if defined(OM_LOCAL_PIDFD_SPAWN)
        const int result=ready&&!cancel.load()&&admission.permitted()?pidfd_spawn(&pidfd,executable.c_str(),&setup.actions,&setup.attributes,argv,environment):-1;
#else
        const int result=-1;(void)ready;(void)cancel;(void)argv;(void)environment;
#endif
        if(result){pidfd=-1;return false;}
        created=true;input=pipe.request[0];pipe.request[0]=-1;output=pipe.reply[0];pipe.reply[0]=-1;sent=0;return true;
    }
    bool send(const std::string &request) {
        if (input < 0) return true;
        const auto n = ::send(input,request.data()+sent,request.size()-sent,MSG_DONTWAIT|MSG_NOSIGNAL);
        if (n < 0) return errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK;
        sent += static_cast<size_t>(n);
        if (sent == request.size()) { ::close(input); input = -1; }
        return true;
    }
    bool read(std::string &wire) {
        for (unsigned chunk = 0; chunk < 8; ++chunk) {
            char data[4096]; const auto n = ::read(output,data,sizeof(data));
            if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
            if (!n) return true;
            if (static_cast<size_t>(n) > localTaskResponseLimit-wire.size()) return false;
            wire.append(data,static_cast<size_t>(n));
        }
        return true;
    }
    bool exited(bool &ok) {
        if(reaped){ok=success;return true;}if(!owned()||unknown)return false;
#if defined(OM_LOCAL_PIDFD_SPAWN)
        siginfo_t info{};const int result=waitid(P_PIDFD,static_cast<id_t>(pidfd),&info,WEXITED|WNOHANG);
        if(result<0&&errno==EINTR)return false;
        if(result||(info.si_pid&&info.si_code!=CLD_EXITED&&info.si_code!=CLD_KILLED&&info.si_code!=CLD_DUMPED)){unknown=true;unreaped();return false;}
        verified=true;if(!info.si_pid)return false;
        reaped=true;success=info.si_code==CLD_EXITED&&info.si_status==0;settled();ok=success;return true;
#else
        (void)ok;unknown=true;unreaped();return false;
#endif
    }
    void terminateOwned(){
#if defined(__linux__) && defined(SYS_pidfd_send_signal)
        ::syscall(SYS_pidfd_send_signal,pidfd,SIGKILL,nullptr,0u);
#endif
    }
#endif
    ~Impl(){close();if(reserved)--reservations();} // deleting owner holds registry mutex
    bool stop(){
        closeChannels();if(!owned()){close();release();return true;}
        bool okay=false;if(exited(okay)){close();release();reaped=success=verified=false;return true;}
        if(unknown){unreaped();return false;}
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);bool signalled=false;
        for(;;){
            if(verified&&!signalled){terminateOwned();signalled=true;}
            if(exited(okay)){close();release();reaped=success=verified=false;return true;}
            if(unknown||std::chrono::steady_clock::now()>=deadline){unreaped();return false;}
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
};
LocalTaskProcess::LocalTaskProcess(std::filesystem::path helper) : impl_(new Impl(std::move(helper))) {}
LocalTaskProcess::~LocalTaskProcess(){
    if(!impl_->stop()){std::lock_guard<std::mutex> lock(Impl::mutex());Impl::quarantine().push_back(impl_.release());}
    else{std::lock_guard<std::mutex> lock(Impl::mutex());impl_.reset();}
}
LocalTaskResult LocalTaskProcess::run(const std::string &request, const std::atomic<bool> &cancel, unsigned ms) {
    LocalTaskResult result;
    if (cancel.load()) { result.status = LocalTaskStatus::Cancelled; return result; }
    if (impl_->busy.test_and_set()) { result.status = LocalTaskStatus::Busy; return result; }
    struct Busy { Impl &p; ~Busy(){p.busy.clear();} } busy{*impl_};
    if (request.empty() || request.size() > localTaskRequestLimit || ms < 50 || ms > 60000 || !localTaskPath(impl_->helper)) { result.status = LocalTaskStatus::Invalid; return result; }
    if (!impl_->stop()) { result.status = LocalTaskStatus::Unreaped; return result; }
    impl_->created=false;
    try {
        const auto deadline = std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
        result.status=Impl::reserve(impl_.get());
        if(result.status==LocalTaskStatus::Ok){
            result.status=LocalTaskStatus::Unavailable;
            if(impl_->start(request,cancel)){
                std::string wire;
                for (;;) {
                    if(!impl_->admission.permitted()){result.status=LocalTaskStatus::Unreaped;break;}
                    if (cancel.load()) { result.status = LocalTaskStatus::Cancelled; break; }
                    if (std::chrono::steady_clock::now() >= deadline) { result.status = LocalTaskStatus::Timeout; break; }
                    if (!impl_->send(request) || !impl_->read(wire)) break;
                    bool ok = false;
                    if (impl_->exited(ok)) {
                        size_t previous = 0;
                        do { previous = wire.size(); if (!impl_->read(wire)) { ok = false; break; } } while (wire.size() != previous);
                        if (cancel.load()) result.status = LocalTaskStatus::Cancelled;
                        else if (std::chrono::steady_clock::now() >= deadline) result.status = LocalTaskStatus::Timeout;
                        else if (ok && !wire.empty()) { result.status = LocalTaskStatus::Ok; result.response = std::move(wire); }
                        break;
                    }
                    if(impl_->unknown){result.status=LocalTaskStatus::Unreaped;break;}
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }else if(cancel.load())result.status=LocalTaskStatus::Cancelled;
        }
    } catch (...) { result.status = LocalTaskStatus::Unavailable; }
    result.childStarted=impl_->created;
    if (!impl_->stop()||Impl::unresolved().load()||nativeProcessUnresolved()||impl_->admission.invalidated()) result.status = LocalTaskStatus::Unreaped;
    if (result.status != LocalTaskStatus::Ok) result.response.clear();
    return result;
}
LocalTaskResult LocalTaskProcess::hold(const std::string &request,const std::atomic<bool> &cancel,unsigned ms,const std::function<bool(const std::string &)> &ready){
    LocalTaskResult result;
    if(cancel.load()){result.status=LocalTaskStatus::Cancelled;return result;}
    if(impl_->busy.test_and_set()){result.status=LocalTaskStatus::Busy;return result;}
    struct Busy {Impl &p;~Busy(){p.busy.clear();}} busy{*impl_};
    if(!ready||request.empty()||request.size()>localTaskRequestLimit||ms<50||ms>60000||!localTaskPath(impl_->helper)){result.status=LocalTaskStatus::Invalid;return result;}
    if(!impl_->stop()){result.status=LocalTaskStatus::Unreaped;return result;}
    impl_->created=false;
    try{
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
        result.status=Impl::reserve(impl_.get());
        if(result.status==LocalTaskStatus::Ok){
            result.status=LocalTaskStatus::Unavailable;
            if(impl_->start(request,cancel)){
                std::string wire;bool accepted=false;size_t frameBytes=0;
                for(;;){
                    if(!impl_->admission.permitted()){result.status=LocalTaskStatus::Unreaped;break;}
                    if(cancel.load()){result.status=LocalTaskStatus::Cancelled;break;}
                    if(!accepted&&std::chrono::steady_clock::now()>=deadline){result.status=LocalTaskStatus::Timeout;break;}
                    if(!impl_->send(request)||!impl_->read(wire))break;
                    bool ok=false;if(impl_->exited(ok))break; // readiness is not ownership after child exit
                    if(impl_->unknown){result.status=LocalTaskStatus::Unreaped;break;}
                    if(cancel.load()){result.status=LocalTaskStatus::Cancelled;break;}
                    if(accepted){if(wire.size()!=frameBytes)break;}
                    else{const auto end=wire.find('\n');if(end!=std::string::npos){
                        if(!end||end+1!=wire.size()||std::chrono::steady_clock::now()>=deadline)break;
                        const bool admit=ready(wire.substr(0,end));
                        if(!impl_->admission.permitted()){result.status=LocalTaskStatus::Unreaped;break;}
                        if(!admit)break;
                        accepted=true;frameBytes=wire.size();}}
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }else if(cancel.load())result.status=LocalTaskStatus::Cancelled;
        }
    }catch(...){result.status=LocalTaskStatus::Unavailable;}
    result.childStarted=impl_->created;
    if(!impl_->stop()||Impl::unresolved().load()||nativeProcessUnresolved()||impl_->admission.invalidated())result.status=LocalTaskStatus::Unreaped;
    result.response.clear();
    return result;
}
bool readLocalTaskInput(int argc, char **argv, std::string *out) {
    if (!out || argc < 2 || std::strcmp(argv[1],"--local-task-v1")) return false;
#if defined(_WIN32)
    if (argc != 4) return false;
    auto integer = [](const char *s, uint64_t &n) {
        n = 0; if (!*s || (*s == '0' && s[1])) return false;
        for (; *s; ++s) { if (*s < '0' || *s > '9' || n > (UINT64_MAX-static_cast<unsigned>(*s-'0'))/10) return false; n = n*10+static_cast<unsigned>(*s-'0'); }
        return true;
    };
    uint64_t handle = 0, size = 0;
    if (!integer(argv[2],handle) || !integer(argv[3],size) || !handle || handle > UINTPTR_MAX || !size || size > localTaskRequestLimit) return false;
    const auto mapping = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(handle));
    const void *view = MapViewOfFile(mapping,FILE_MAP_READ,0,0,static_cast<SIZE_T>(size));
    if (!view) { CloseHandle(mapping); return false; }
    out->assign(static_cast<const char *>(view),static_cast<size_t>(size)); UnmapViewOfFile(view); CloseHandle(mapping);
#else
    if (argc != 2) return false;
    out->clear();
    for (;;) {
        char data[4096]; const auto n = ::read(0,data,sizeof(data));
        if (n < 0) { if (errno == EINTR) continue; return false; }
        if (!n) break;
        if (static_cast<size_t>(n) > localTaskRequestLimit-out->size()) return false;
        out->append(data,static_cast<size_t>(n));
    }
#endif
    return !out->empty();
}
bool writeLocalTaskOutput(const std::string &out) {
    if (out.empty() || out.size() > localTaskResponseLimit) return false;
#if defined(_WIN32)
    if (_setmode(_fileno(stdout),_O_BINARY) < 0) return false;
#endif
    return std::fwrite(out.data(),1,out.size(),stdout) == out.size() && std::fflush(stdout) == 0;
}
}  // namespace om
