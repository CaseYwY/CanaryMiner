#include "public_json_process.h"
#include <chrono>
#include <thread>
#include <vector>
#include <sstream>
#include <locale>
#include <algorithm>

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
#if defined(_WIN32)
    HANDLE process = nullptr, output = nullptr, job = nullptr;
    bool stop() {
        if (!process) return true;
        // Job close kills this helper and any descendants, never a mining worker.
        if (job) { CloseHandle(job); job = nullptr; }
        else TerminateProcess(process, 1);
        if (WaitForSingleObject(process, 250) != WAIT_OBJECT_0) return false;
        CloseHandle(process); process = nullptr;
        if (output) { CloseHandle(output); output = nullptr; }
        return true;
    }
    ~Impl() {
        stop();
        if (process) CloseHandle(process);
        if (output) CloseHandle(output);
    }
    bool start(const std::vector<std::string> &args) {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE read = nullptr, write = nullptr, null = INVALID_HANDLE_VALUE;
        if (!CreatePipe(&read, &write, &sa, 0)) return false;
        if (!SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0)) { CloseHandle(read); CloseHandle(write); return false; }
        null = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION policy{};
        policy.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        policy.BasicLimitInformation.ActiveProcessLimit = 1;
        bool ready = null != INVALID_HANDLE_VALUE && job && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &policy, sizeof(policy));
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<unsigned char> buffer(bytes);
        auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
        const bool initialized = ready && InitializeProcThreadAttributeList(attributes, 1, 0, &bytes);
        HANDLE inherit[] = {write, null};
        ready = initialized && UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), nullptr, nullptr);
        STARTUPINFOEXW si{};
        si.StartupInfo.cb = sizeof(si); si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = null; si.StartupInfo.hStdOutput = write; si.StartupInfo.hStdError = null; si.lpAttributeList = attributes;
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
        bool created = ready && CreateProcessW(helper.c_str(), cmd.data(), nullptr, nullptr, TRUE,
            EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | CREATE_SUSPENDED,
            &environment[0], parent.c_str(), &si.StartupInfo, &pi);
        if (initialized) DeleteProcThreadAttributeList(attributes);
        CloseHandle(write);
        if (null != INVALID_HANDLE_VALUE) CloseHandle(null);
        if (created) {
            process = pi.hProcess; output = read;
            created = AssignProcessToJobObject(job, process) && ResumeThread(pi.hThread) != static_cast<DWORD>(-1);
            CloseHandle(pi.hThread);
            if (!created) { TerminateProcess(process, 1); stop(); }
        } else { CloseHandle(read); if (job) { CloseHandle(job); job = nullptr; } }
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
        if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0) return false;
        DWORD code = 1;
        *ok = GetExitCodeProcess(process, &code) && code == 0;
        return true;
    }
#else
    pid_t pid = -1;
    int output = -1;
    bool reaped = false;
    bool stop() {
        if (pid < 0) return true;
        if (!reaped) {
            ::kill(-pid, SIGKILL);
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
            for (;;) {
                int status = 0;
                const auto result = waitpid(pid, &status, WNOHANG);
                if (result == pid || (result < 0 && errno == ECHILD)) { reaped = true; break; }
                if (std::chrono::steady_clock::now() >= until) return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        if (output >= 0) { ::close(output); output = -1; }
        pid = -1; reaped = false; return true;
    }
    ~Impl() { stop(); if (output >= 0) ::close(output); }
    bool start(const std::vector<std::string> &args) {
        int pipe[2];
        if (pipe2(pipe, O_CLOEXEC) != 0) return false;
        if (fcntl(pipe[0], F_SETFL, O_NONBLOCK) != 0) { ::close(pipe[0]); ::close(pipe[1]); return false; }
        posix_spawn_file_actions_t actions;
        posix_spawnattr_t attributes;
        if (posix_spawn_file_actions_init(&actions) != 0) { ::close(pipe[0]); ::close(pipe[1]); return false; }
        if (posix_spawnattr_init(&attributes) != 0) { posix_spawn_file_actions_destroy(&actions); ::close(pipe[0]); ::close(pipe[1]); return false; }
        sigset_t mask, defaults;
        sigemptyset(&mask); sigemptyset(&defaults); sigaddset(&defaults, SIGPIPE); sigaddset(&defaults, SIGINT); sigaddset(&defaults, SIGTERM);
        bool ready = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0) == 0 &&
            posix_spawn_file_actions_adddup2(&actions, pipe[1], 1) == 0 &&
            posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0) == 0;
#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 34)
        ready = ready && posix_spawn_file_actions_addclosefrom_np(&actions, 3) == 0;
#else
        ready = false;  // cannot prove unrelated miner descriptors are closed on this platform
#endif
#else
        ready = false;
#endif
        ready = ready && posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF) == 0 &&
            posix_spawnattr_setpgroup(&attributes, 0) == 0 && posix_spawnattr_setsigmask(&attributes, &mask) == 0 &&
            posix_spawnattr_setsigdefault(&attributes, &defaults) == 0;
        const auto executable = helper.string();
        std::vector<char *> argv{const_cast<char *>(executable.c_str())};
        for (const auto &arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
        argv.push_back(nullptr);
        char lang[] = "LANG=C", locale[] = "LC_ALL=C";
        char *environment[] = {lang, locale, nullptr};
        const int result = ready ? posix_spawn(&pid, executable.c_str(), &actions, &attributes, argv.data(), environment) : -1;
        posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attributes);
        ::close(pipe[1]);
        if (result != 0) { ::close(pipe[0]); pid = -1; return false; }
        output = pipe[0]; return true;
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
        int status = 0;
        if (waitpid(pid, &status, WNOHANG) != pid) return false;
        reaped = true; *ok = WIFEXITED(status) && WEXITSTATUS(status) == 0; return true;
    }
#endif
};

PublicJsonProcessClient::PublicJsonProcessClient(std::filesystem::path helper) : impl_(new Impl(std::move(helper))) {}
PublicJsonProcessClient::~PublicJsonProcessClient() = default;
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
    if (!impl_->stop()) return response;  // do not spawn another helper while a previous child has not exited
    try {
#if defined(_WIN32)
        // Installed local helper only. A UNC/device path could itself block on network I/O before spawn.
        const auto root = impl_->helper.root_name().native();
        if (root.size() != 2 || root[1] != L':' || impl_->helper.native().find(L'\"') != std::wstring::npos) return response;
#endif
        if (!impl_->helper.is_absolute() || !std::filesystem::is_regular_file(impl_->helper)) return response;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(limits.totalMs);
        const std::vector<std::string> args = {std::to_string(static_cast<unsigned>(request.feed)), std::to_string(request.fromUtc), std::to_string(request.toUtc),
            std::to_string(limits.connectMs), std::to_string(limits.totalMs), std::to_string(limits.bodyBytes), std::to_string(limits.headerBytes)};
        if (!impl_->start(args)) return response;
        struct Stop { Impl *p; ~Stop() { p->stop(); } } stop{impl_.get()};
        std::string wire;
        const size_t limit = limits.bodyBytes + kWireHeader;
        for (;;) {
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
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } catch (...) { return response; }
}
}  // namespace om
