#include "public_json_process.h"
#include <chrono>
#include <thread>
#include <vector>
#include <sstream>
#include <locale>
#include <algorithm>
#include <mutex>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#if defined(__linux__)
#include <sys/syscall.h>
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 40)
#define OM_PUBLIC_PIDFD_SPAWN 1
#endif
#endif
#endif
#include <unistd.h>
#endif

namespace om {
namespace {
constexpr size_t kWireHeader = 128;
bool integer(const std::string &s, uint64_t *result) {
    if (s.empty() || s.size() > 20 || (s.size() > 1 && s[0] == '0')) return false;
    uint64_t n = 0;
    for (char ch : s) {
        if (ch < '0' || ch > '9' || n > (UINT64_MAX - static_cast<unsigned>(ch - '0')) / 10) return false;
        n = n * 10 + static_cast<unsigned>(ch - '0');
    }
    *result = n; return true;
}
bool validResponse(const PublicJsonResponse &r, size_t limit) {
    if (static_cast<unsigned>(r.status) > static_cast<unsigned>(PublicHttpsStatus::InvalidJson) || r.httpStatus < 0 || r.httpStatus > 599 ||
        r.retryAfterSeconds > 3600 || r.body.size() > limit) return false;
    if (r.status == PublicHttpsStatus::Ok)
        return r.httpStatus == 200 && r.observedUtc > 0 && r.observedUtc <= 253402300799ULL && boundedPublicJson(r.body);
    return r.body.empty() && r.observedUtc == 0;
}
}  // namespace

bool encodePublicJsonResponse(const PublicJsonResponse &r, std::string *wire) {
    if (!wire || !validResponse(r, 1024 * 1024)) return false;
    *wire = "CMJSON1 " + std::to_string(static_cast<unsigned>(r.status)) + " " + std::to_string(r.httpStatus) + " " +
        std::to_string(r.observedUtc) + " " + std::to_string(r.retryAfterSeconds) + " " + std::to_string(r.body.size()) + "\n" + r.body;
    return true;
}
bool decodePublicJsonResponse(const std::string &wire, size_t limit, PublicJsonResponse *response) {
    if (!response || !limit || limit > 1024 * 1024 || wire.size() > limit + kWireHeader) return false;
    const auto end = wire.find('\n');
    if (end == std::string::npos || end > kWireHeader) return false;
    std::istringstream header(wire.substr(0, end));
    header.imbue(std::locale::classic());
    std::string magic, fields[5], extra;
    if (!(header >> magic >> fields[0] >> fields[1] >> fields[2] >> fields[3] >> fields[4]) || magic != "CMJSON1" || header >> extra) return false;
    uint64_t n[5] = {};
    for (unsigned i = 0; i < 5; ++i) if (!integer(fields[i], &n[i])) return false;
    if (n[0] > static_cast<unsigned>(PublicHttpsStatus::InvalidJson) || n[1] > 599 || n[3] > 3600 || n[4] > limit || n[4] != wire.size() - end - 1) return false;
    PublicJsonResponse r;
    r.status = static_cast<PublicHttpsStatus>(n[0]); r.httpStatus = static_cast<long>(n[1]); r.observedUtc = n[2];
    r.retryAfterSeconds = static_cast<unsigned>(n[3]); r.body = wire.substr(end + 1);
    if (!validResponse(r, limit)) return false;
    *response = std::move(r); return true;
}

struct PublicJsonProcessClient::Impl {
    explicit Impl(std::filesystem::path path) : helper(std::move(path)) {}
    std::filesystem::path helper;
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
    bool reaped = false, success = false, unknown = false, verified = false, blocked = false, reserved = false;
    static std::mutex &mutex() { static std::mutex value; return value; }
    static std::vector<Impl *> &quarantine() { static auto *value = new std::vector<Impl *>; return *value; }
    static unsigned &reservations() { static unsigned value = 0; return value; }
    static std::atomic<unsigned> &unresolved() { static std::atomic<unsigned> value{0}; return value; }
    void unreaped() { if (!blocked) { blocked = true; ++unresolved(); } }
    void settled() { if (blocked) { blocked = false; --unresolved(); } }
    static PublicHttpsStatus reserve(Impl *p) {
        std::lock_guard<std::mutex> lock(mutex());
        auto &held = quarantine();
        if (held.capacity() < 64) held.reserve(64);  // no allocation during unknown-owner teardown
        for (auto i = held.begin(); i != held.end();) {
            bool ok = false;
            if ((*i)->exited(&ok)) { (*i)->close(); delete *i; i = held.erase(i); }
            else ++i;
        }
        if (!held.empty() || unresolved().load()) return PublicHttpsStatus::Unreaped;
        if (reservations() >= 64) return PublicHttpsStatus::Busy;
        ++reservations(); p->reserved = true; return PublicHttpsStatus::Ok;
    }
    void release() {
        std::lock_guard<std::mutex> lock(mutex());
        if (reserved) { reserved = false; --reservations(); }
    }
#if defined(_WIN32)
    HANDLE process = nullptr, output = nullptr, job = nullptr;
    bool jobBound = false;
    static void closeHandle(HANDLE &h) { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); h = nullptr; }
    bool owned() const { return process != nullptr; }
    void close() {
        closeHandle(process); closeHandle(output); closeHandle(job);
        jobBound = false;
    }
    bool start(const std::vector<std::string> &args, const std::atomic<bool> &cancel) {
        struct Temps {
            HANDLE read = nullptr, write = nullptr, null = nullptr, thread = nullptr;
            ~Temps() { closeHandle(read); closeHandle(write); closeHandle(null); closeHandle(thread); }
        } temp;
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        if (!CreatePipe(&temp.read, &temp.write, &sa, 0)) return false;
        if (!SetHandleInformation(temp.read, HANDLE_FLAG_INHERIT, 0)) return false;
        temp.null = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION policy{};
        policy.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        policy.BasicLimitInformation.ActiveProcessLimit = 1;
        bool ready = temp.null != INVALID_HANDLE_VALUE && job && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &policy, sizeof(policy));
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<unsigned char> buffer(bytes);
        auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
        const bool initialized = ready && InitializeProcThreadAttributeList(attributes, 1, 0, &bytes);
        struct AttributeCleanup { LPPROC_THREAD_ATTRIBUTE_LIST value; ~AttributeCleanup() { if (value) DeleteProcThreadAttributeList(value); } } cleanup{initialized ? attributes : nullptr};
        HANDLE inherit[] = {temp.write, temp.null};
        ready = initialized && UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), nullptr, nullptr);
        STARTUPINFOEXW si{};
        si.StartupInfo.cb = sizeof(si); si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = temp.null; si.StartupInfo.hStdOutput = temp.write; si.StartupInfo.hStdError = temp.null; si.lpAttributeList = attributes;
        // Only fixed numeric args. No shell, PATH lookup, arbitrary quoting, inherited user environment or console.
        std::wstring command = L"\"" + helper.wstring() + L"\"";
        for (const auto &arg : args) command += L" " + std::wstring(arg.begin(), arg.end());
        std::vector<wchar_t> cmd(command.begin(), command.end()); cmd.push_back(0);
        wchar_t systemRoot[32768] = {};
        const DWORD length = GetWindowsDirectoryW(systemRoot, 32768);
        std::wstring environment = L"SystemRoot=";
        if (!length || length >= 32768) ready = false;
        environment += systemRoot; environment.push_back(0); environment.push_back(0);
        PROCESS_INFORMATION pi{};
        const auto parent = helper.parent_path().wstring();
        bool created = ready && !cancel.load() && !unresolved().load() && CreateProcessW(helper.c_str(), cmd.data(), nullptr, nullptr, TRUE,
            EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | CREATE_SUSPENDED,
            &environment[0], parent.c_str(), &si.StartupInfo, &pi);
        if (created) {
            process = pi.hProcess; output = temp.read; temp.read = nullptr; temp.thread = pi.hThread;
            jobBound = AssignProcessToJobObject(job, process) != 0;
            created = jobBound && !cancel.load() && !unresolved().load() && ResumeThread(pi.hThread) != static_cast<DWORD>(-1);
            // The common cleanup retains both native handles until exit is actually observed.
        }
        return created;
    }
    bool read(std::string *wire, size_t limit) {
        for (unsigned chunks = 0; chunks < 8; ++chunks) {
            DWORD available = 0;
            if (!PeekNamedPipe(output, nullptr, 0, nullptr, &available, nullptr)) return GetLastError() == ERROR_BROKEN_PIPE;
            if (!available) return true;
            char data[4096]; DWORD count = 0;
            if (!ReadFile(output, data, (std::min)(available, static_cast<DWORD>(sizeof(data))), &count, nullptr)) return false;
            if (count > limit - wire->size()) return false;
            wire->append(data, count);
        }
        return true;
    }
    bool exited(bool *ok) {
        if (reaped) { *ok = success; return true; }
        if (!owned() || unknown) return false;
        const auto result = WaitForSingleObject(process, 0);
        if (result == WAIT_TIMEOUT) { verified = true; return false; }
        DWORD code = 1;
        if (result != WAIT_OBJECT_0 || !GetExitCodeProcess(process, &code)) { unknown = true; unreaped(); return false; }
        reaped = true; success = code == 0; settled(); *ok = success; return true;
    }
    void terminateOwned() { if (jobBound) TerminateJobObject(job, 1); else TerminateProcess(process, 1); }
#else
    int output = -1, pidfd = -1;
    bool owned() const { return pidfd >= 0; }
    void close() {
        if (output >= 0) { ::close(output); output = -1; }
        if (pidfd >= 0) { ::close(pidfd); pidfd = -1; }
    }
    bool start(const std::vector<std::string> &args, const std::atomic<bool> &cancel) {
        struct sigaction policy{};
        if (sigaction(SIGCHLD, nullptr, &policy) || policy.sa_handler != SIG_DFL || (policy.sa_flags & SA_NOCLDWAIT)) return false;
#if !defined(OM_PUBLIC_PIDFD_SPAWN) || !defined(SYS_pidfd_send_signal)
        return false;
#endif
        struct Pipes { int fd[2] = {-1, -1}; ~Pipes() { for (int value : fd) if (value >= 0) ::close(value); } } pipe;
        if (pipe2(pipe.fd, O_CLOEXEC) != 0 || fcntl(pipe.fd[0], F_SETFL, O_NONBLOCK) != 0) return false;
        struct Setup {
            posix_spawn_file_actions_t actions; posix_spawnattr_t attributes;
            bool actionsReady = false, attributesReady = false;
            ~Setup() { if (attributesReady) posix_spawnattr_destroy(&attributes); if (actionsReady) posix_spawn_file_actions_destroy(&actions); }
        } setup;
        if (posix_spawn_file_actions_init(&setup.actions) != 0) return false;
        setup.actionsReady = true;
        if (posix_spawnattr_init(&setup.attributes) != 0) return false;
        setup.attributesReady = true;
        sigset_t mask, defaults;
        sigemptyset(&mask); sigemptyset(&defaults); sigaddset(&defaults, SIGPIPE); sigaddset(&defaults, SIGINT); sigaddset(&defaults, SIGTERM);
        bool ready = posix_spawn_file_actions_addopen(&setup.actions, 0, "/dev/null", O_RDONLY, 0) == 0 &&
            posix_spawn_file_actions_adddup2(&setup.actions, pipe.fd[1], 1) == 0 &&
            posix_spawn_file_actions_addopen(&setup.actions, 2, "/dev/null", O_WRONLY, 0) == 0;
#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 34)
        ready = ready && posix_spawn_file_actions_addclosefrom_np(&setup.actions, 3) == 0;
#else
        ready = false;  // cannot prove unrelated miner descriptors are closed on this platform
#endif
#else
        ready = false;
#endif
        ready = ready && posix_spawnattr_setflags(&setup.attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF) == 0 &&
            posix_spawnattr_setpgroup(&setup.attributes, 0) == 0 && posix_spawnattr_setsigmask(&setup.attributes, &mask) == 0 &&
            posix_spawnattr_setsigdefault(&setup.attributes, &defaults) == 0;
        const auto executable = helper.string();
        std::vector<char *> argv{const_cast<char *>(executable.c_str())};
        for (const auto &arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
        argv.push_back(nullptr);
        char lang[] = "LANG=C", locale[] = "LC_ALL=C";
        char *environment[] = {lang, locale, nullptr};
        // Obtain the kernel identity atomically with creation. A later pidfd_open(pid) is still a reuse race.
#if defined(OM_PUBLIC_PIDFD_SPAWN)
        const int result = ready && !cancel.load() && !unresolved().load() ? pidfd_spawn(&pidfd, executable.c_str(), &setup.actions, &setup.attributes, argv.data(), environment) : -1;
#else
        const int result = -1;
        (void)ready; (void)cancel; (void)environment;
#endif
        if (result != 0) { pidfd = -1; return false; }
        output = pipe.fd[0]; pipe.fd[0] = -1;
        return true;
    }
    bool read(std::string *wire, size_t limit) {
        for (unsigned chunks = 0; chunks < 8; ++chunks) {
            char data[4096];
            const auto count = ::read(output, data, sizeof(data));
            if (count < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
            if (!count) return true;
            if (static_cast<size_t>(count) > limit - wire->size()) return false;
            wire->append(data, static_cast<size_t>(count));
        }
        return true;
    }
    bool exited(bool *ok) {
        if (reaped) { *ok = success; return true; }
        if (!owned() || unknown) return false;
#if defined(OM_PUBLIC_PIDFD_SPAWN)
        siginfo_t info{};
        const int result = waitid(P_PIDFD, static_cast<id_t>(pidfd), &info, WEXITED | WNOHANG);
        if (result < 0 && errno == EINTR) return false;
        if (result != 0 || (info.si_pid && info.si_code != CLD_EXITED && info.si_code != CLD_KILLED && info.si_code != CLD_DUMPED)) {
            unknown = true; unreaped(); return false;
        }
        verified = true;  // the pinned identity is a child, not merely a potentially reused PID
        if (!info.si_pid) return false;
        reaped = true; success = info.si_code == CLD_EXITED && info.si_status == 0; settled(); *ok = success; return true;
#else
        (void)ok; unknown = true; unreaped(); return false;
#endif
    }
    void terminateOwned() {
#if defined(__linux__) && defined(SYS_pidfd_send_signal)
        // A competing reaper cannot turn this retained kernel identity into an unrelated signal target.
        ::syscall(SYS_pidfd_send_signal, pidfd, SIGKILL, nullptr, 0u);
#endif
    }
#endif
    ~Impl() { close(); if (reserved) --reservations(); }  // deleting owner holds the registry mutex
    bool stop() {
        if (!owned()) { close(); release(); return true; }
        bool ok = false;
        if (exited(&ok)) { close(); release(); reaped = success = verified = false; return true; }
        if (unknown) { unreaped(); return false; }
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        bool signalled = false;
        for (;;) {
            if (verified && !signalled) { terminateOwned(); signalled = true; }
            if (exited(&ok)) { close(); release(); reaped = success = verified = false; return true; }
            if (unknown || std::chrono::steady_clock::now() >= until) { unreaped(); return false; }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
};

PublicJsonProcessClient::PublicJsonProcessClient(std::filesystem::path helper) : impl_(new Impl(std::move(helper))) {}
PublicJsonProcessClient::~PublicJsonProcessClient() {
    if (!impl_->stop()) {
        std::lock_guard<std::mutex> lock(Impl::mutex()); Impl::quarantine().push_back(impl_.release());
    } else { std::lock_guard<std::mutex> lock(Impl::mutex()); impl_.reset(); }
}
PublicJsonResponse PublicJsonProcessClient::get(const PublicJsonRequest &request, const std::atomic<bool> &cancel, PublicHttpsLimits limits) {
    PublicJsonResponse response;
    if (cancel.load()) { response.status = PublicHttpsStatus::Cancelled; return response; }
    if (impl_->busy.test_and_set()) { response.status = PublicHttpsStatus::Busy; return response; }
    struct Busy { Impl *p; ~Busy() { p->busy.clear(); } } busy{impl_.get()};
    std::string ignored;
    if (!publicFeedUrl(request, &ignored) || limits.totalMs < 50 || limits.totalMs > 60000 || !limits.connectMs || limits.connectMs > limits.totalMs ||
        !limits.bodyBytes || limits.bodyBytes > 1024 * 1024 || limits.headerBytes < 64 || limits.headerBytes > 32768) {
        response.status = PublicHttpsStatus::InvalidRequest; return response;
    }
    if (!impl_->stop()) { response.status = PublicHttpsStatus::Unreaped; return response; }
    try {
        response = [&]() -> PublicJsonResponse {
#if defined(_WIN32)
        // Installed local helper only. A UNC/device path could itself block on network I/O before spawn.
        const auto root = impl_->helper.root_name().native();
        if (root.size() != 2 || root[1] != L':' || impl_->helper.native().find(L'\"') != std::wstring::npos) return response;
#endif
        if (!impl_->helper.is_absolute() || !std::filesystem::is_regular_file(impl_->helper)) return response;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(limits.totalMs);
        const std::vector<std::string> args = {std::to_string(static_cast<unsigned>(request.feed)), std::to_string(request.fromUtc), std::to_string(request.toUtc),
            std::to_string(limits.connectMs), std::to_string(limits.totalMs), std::to_string(limits.bodyBytes), std::to_string(limits.headerBytes)};
        response.status = Impl::reserve(impl_.get());
        if (response.status != PublicHttpsStatus::Ok) return response;
        response = {};
        if (!impl_->start(args, cancel)) {
            if (cancel.load()) response.status = PublicHttpsStatus::Cancelled;
            else if (Impl::unresolved().load()) response.status = PublicHttpsStatus::Unreaped;
            return response;
        }
        std::string wire;
        const size_t limit = limits.bodyBytes + kWireHeader;
        for (;;) {
            if (Impl::unresolved().load()) { response = {}; response.status = PublicHttpsStatus::Unreaped; return response; }
            if (cancel.load()) { response.status = PublicHttpsStatus::Cancelled; return response; }
            if (std::chrono::steady_clock::now() >= deadline) { response.status = PublicHttpsStatus::Timeout; return response; }
            if (!impl_->read(&wire, limit)) return response;
            bool ok = false;
            if (impl_->exited(&ok)) {
                // Drain bytes produced immediately before exit; never wait for EOF or inherited writer handles.
                while (wire.size() <= limit) {
                    const size_t before = wire.size();
                    if (!impl_->read(&wire, limit)) return response;
                    if (before == wire.size()) break;
                }
                if (!ok || !decodePublicJsonResponse(wire, limits.bodyBytes, &response)) return PublicJsonResponse{};
                if (cancel.load()) { response = {}; response.status = PublicHttpsStatus::Cancelled; }
                else if (std::chrono::steady_clock::now() >= deadline) { response = {}; response.status = PublicHttpsStatus::Timeout; }
                return response;
            }
            if (impl_->unknown) { response = {}; response.status = PublicHttpsStatus::Unreaped; return response; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        }();
    } catch (...) { response = {}; }
    // Cleanup is part of the result, not a destructor running after the return value was already copied.
    if (!impl_->stop() || Impl::unresolved().load()) { response = {}; response.status = PublicHttpsStatus::Unreaped; }
    return response;
}
}  // namespace om
