#include "local_task_process.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
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
#endif

namespace om {
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
#if defined(_WIN32)
    HANDLE process = nullptr, output = nullptr, job = nullptr;
    bool stop() {
        if (!process) return true;
        if (job) { CloseHandle(job); job = nullptr; } else TerminateProcess(process,1);
        if (WaitForSingleObject(process,250) != WAIT_OBJECT_0) return false;
        CloseHandle(process); process = nullptr;
        if (output) { CloseHandle(output); output = nullptr; }
        return true;
    }
    ~Impl() { stop(); if (process) CloseHandle(process); if (output) CloseHandle(output); }
    bool start(const std::string &request) {
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
        HANDLE read = nullptr, write = nullptr, section = nullptr, inherited = nullptr, null = INVALID_HANDLE_VALUE;
        if (!CreatePipe(&read,&write,&sa,0)) return false;
        bool ready = SetHandleInformation(read,HANDLE_FLAG_INHERIT,0) != 0;
        section = CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,static_cast<DWORD>(request.size()),nullptr);
        void *view = section ? MapViewOfFile(section,FILE_MAP_WRITE,0,0,request.size()) : nullptr;
        if (view) { std::memcpy(view,request.data(),request.size()); UnmapViewOfFile(view); }
        ready = ready && view && DuplicateHandle(GetCurrentProcess(),section,GetCurrentProcess(),&inherited,FILE_MAP_READ,TRUE,0);
        if (section) CloseHandle(section);
        null = CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
        job = CreateJobObjectW(nullptr,nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION policy{};
        policy.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        policy.BasicLimitInformation.ActiveProcessLimit = 1;
        ready = ready && null != INVALID_HANDLE_VALUE && job && SetInformationJobObject(job,JobObjectExtendedLimitInformation,&policy,sizeof(policy));
        SIZE_T bytes = 0; InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
        std::vector<unsigned char> buffer(bytes);
        auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
        const bool initialized = ready && InitializeProcThreadAttributeList(attributes,1,0,&bytes);
        HANDLE inherit[] = {write,null,inherited};
        ready = initialized && UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherit,sizeof(inherit),nullptr,nullptr);
        STARTUPINFOEXW si{}; si.StartupInfo.cb = sizeof(si); si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = null; si.StartupInfo.hStdOutput = write; si.StartupInfo.hStdError = null; si.lpAttributeList = attributes;
        // Request paths, bindings and credit IDs remain inside an anonymous read-only child mapping, never command arguments.
        const std::wstring command = L"\"" + helper.wstring() + L"\" --local-task-v1 " + std::to_wstring(reinterpret_cast<uintptr_t>(inherited)) + L" " + std::to_wstring(request.size());
        std::vector<wchar_t> cmd(command.begin(),command.end()); cmd.push_back(0);
        wchar_t directory[32768] = {}; const DWORD length = GetWindowsDirectoryW(directory,32768);
        if (!length || length >= 32768) ready = false;
        std::wstring environment = L"SystemRoot="; environment += directory; environment.push_back(0); environment.push_back(0);
        PROCESS_INFORMATION pi{}; const auto parent = helper.parent_path().wstring();
        bool created = ready && CreateProcessW(helper.c_str(),cmd.data(),nullptr,nullptr,TRUE,
            EXTENDED_STARTUPINFO_PRESENT|CREATE_UNICODE_ENVIRONMENT|CREATE_NO_WINDOW|CREATE_SUSPENDED,&environment[0],parent.c_str(),&si.StartupInfo,&pi);
        if (initialized) DeleteProcThreadAttributeList(attributes);
        CloseHandle(write); if (null != INVALID_HANDLE_VALUE) CloseHandle(null); if (inherited) CloseHandle(inherited);
        if (created) {
            process = pi.hProcess; output = read;
            created = AssignProcessToJobObject(job,process) && ResumeThread(pi.hThread) != static_cast<DWORD>(-1);
            CloseHandle(pi.hThread);
            if (!created) { TerminateProcess(process,1); stop(); }
        } else { CloseHandle(read); if (job) { CloseHandle(job); job = nullptr; } }
        return created;
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
        if (WaitForSingleObject(process,0) != WAIT_OBJECT_0) return false;
        DWORD code = 1; ok = GetExitCodeProcess(process,&code) && code == 0; return true;
    }
#else
    pid_t pid = -1;
    int input = -1, output = -1;
    size_t sent = 0;
    bool reaped = false;
    bool stop() {
        if (pid < 0) return true;
        if (!reaped) {
            // Do not signal a numeric PID/group after another reaper has already consumed our child.
            int priorStatus = 0; const auto prior = waitpid(pid,&priorStatus,WNOHANG);
            if (prior == pid || (prior < 0 && errno == ECHILD)) reaped = true;
            else if (prior < 0 && errno != EINTR) return false;
            if (!reaped) ::kill(-pid,SIGKILL);
            const auto deadline = std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
            while (!reaped) {
                int status = 0; const auto r = waitpid(pid,&status,WNOHANG);
                if (r == pid || (r < 0 && errno == ECHILD)) { reaped = true; break; }
                if (std::chrono::steady_clock::now() >= deadline) return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        if (input >= 0) { ::close(input); input = -1; }
        if (output >= 0) { ::close(output); output = -1; }
        pid = -1; reaped = false; sent = 0; return true;
    }
    ~Impl() { stop(); if (input >= 0) ::close(input); if (output >= 0) ::close(output); }
    bool start(const std::string &) {
        struct sigaction policy{};
        if (sigaction(SIGCHLD,nullptr,&policy) != 0 || policy.sa_handler != SIG_DFL || (policy.sa_flags & SA_NOCLDWAIT)) return false;
        int request[2], reply[2];
        if (socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,request) != 0) return false;
        if (pipe2(reply,O_CLOEXEC) != 0) { ::close(request[0]); ::close(request[1]); return false; }
        bool ready = fcntl(reply[0],F_SETFL,O_NONBLOCK) == 0;
        posix_spawn_file_actions_t actions; posix_spawnattr_t attributes;
        if (posix_spawn_file_actions_init(&actions) != 0) { for (int fd : {request[0],request[1],reply[0],reply[1]}) ::close(fd); return false; }
        if (posix_spawnattr_init(&attributes) != 0) { posix_spawn_file_actions_destroy(&actions); for (int fd : {request[0],request[1],reply[0],reply[1]}) ::close(fd); return false; }
        ready = ready && posix_spawn_file_actions_adddup2(&actions,request[1],0) == 0 && posix_spawn_file_actions_adddup2(&actions,reply[1],1) == 0 &&
            posix_spawn_file_actions_addopen(&actions,2,"/dev/null",O_WRONLY,0) == 0;
#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2,34)
        ready = ready && posix_spawn_file_actions_addclosefrom_np(&actions,3) == 0;
#else
        ready = false;
#endif
#else
        ready = false;
#endif
        sigset_t mask, defaults; sigemptyset(&mask); sigemptyset(&defaults); sigaddset(&defaults,SIGPIPE); sigaddset(&defaults,SIGINT); sigaddset(&defaults,SIGTERM);
        ready = ready && posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP|POSIX_SPAWN_SETSIGMASK|POSIX_SPAWN_SETSIGDEF) == 0 &&
            posix_spawnattr_setpgroup(&attributes,0) == 0 && posix_spawnattr_setsigmask(&attributes,&mask) == 0 && posix_spawnattr_setsigdefault(&attributes,&defaults) == 0;
        const auto executable = helper.string(); char flag[] = "--local-task-v1";
        char *argv[] = {const_cast<char *>(executable.c_str()),flag,nullptr};
        char lang[] = "LANG=C", locale[] = "LC_ALL=C"; char *environment[] = {lang,locale,nullptr};
        const int result = ready ? posix_spawn(&pid,executable.c_str(),&actions,&attributes,argv,environment) : -1;
        posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attributes);
        ::close(request[1]); ::close(reply[1]);
        if (result != 0) { ::close(request[0]); ::close(reply[0]); pid = -1; return false; }
        input = request[0]; output = reply[0]; sent = 0; return true;
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
        int status = 0; const auto result = waitpid(pid,&status,WNOHANG);
        if (result < 0 && errno == ECHILD) { reaped = true; ok = false; return true; }
        if (result != pid) return false;
        reaped = true; ok = WIFEXITED(status) && WEXITSTATUS(status) == 0; return true;
    }
#endif
};
LocalTaskProcess::LocalTaskProcess(std::filesystem::path helper) : impl_(new Impl(std::move(helper))) {}
LocalTaskProcess::~LocalTaskProcess() = default;
LocalTaskResult LocalTaskProcess::run(const std::string &request, const std::atomic<bool> &cancel, unsigned ms) {
    LocalTaskResult result;
    if (cancel.load()) { result.status = LocalTaskStatus::Cancelled; return result; }
    if (impl_->busy.test_and_set()) { result.status = LocalTaskStatus::Busy; return result; }
    struct Busy { Impl &p; ~Busy(){p.busy.clear();} } busy{*impl_};
    if (request.empty() || request.size() > localTaskRequestLimit || ms < 50 || ms > 60000 || !localTaskPath(impl_->helper)) { result.status = LocalTaskStatus::Invalid; return result; }
    if (!impl_->stop()) { result.status = LocalTaskStatus::Unreaped; return result; }
    try {
        const auto deadline = std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
        if (!impl_->start(request)) return result;
        result.childStarted = true;
        std::string wire;
        for (;;) {
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
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    } catch (...) { result.status = LocalTaskStatus::Unavailable; }
    if (!impl_->stop()) result.status = LocalTaskStatus::Unreaped;
    if (result.status != LocalTaskStatus::Ok) result.response.clear();
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
