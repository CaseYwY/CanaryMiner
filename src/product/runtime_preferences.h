// Local runtime defaults. Separate from presentation, mining credentials and durable counters.
#pragma once

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "../core/host_work_wait.h"
#include "../core/platform.h"
#include "json_dom.h"
#if defined(_WIN32)
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace om {

enum class ChangeActivation { Presentation, WorkBoundary, NextStart, SeparatePermission };
inline const char *activationName(ChangeActivation value) {
    switch (value) {
        case ChangeActivation::Presentation: return "live presentation";
        case ChangeActivation::WorkBoundary: return "safe work boundary";
        case ChangeActivation::NextStart: return "next explicit start; no automatic restart";
        case ChangeActivation::SeparatePermission: return "separate permission required";
    }
    return "unsupported";
}

struct RuntimePreferences {
    unsigned workWaitMs = 0;
    bool historyEnabled = true;
    bool operator==(const RuntimePreferences &other) const { return workWaitMs == other.workWaitMs && historyEnabled == other.historyEnabled; }
    bool operator!=(const RuntimePreferences &other) const { return !(*this == other); }
};

struct RuntimeContext {
    bool running = false;
    RuntimePreferences effective;
    std::optional<unsigned> cliWorkWait;
    bool cliNoHistory = false;
};

inline RuntimePreferences effectiveRuntimePreferences(RuntimePreferences configured, const RuntimeContext &context) {
    if (context.cliWorkWait) configured.workWaitMs = *context.cliWorkWait;
    if (context.cliNoHistory) configured.historyEnabled = false;
    return configured;
}

inline std::string runtimePreferencesJson(const RuntimePreferences &value) {
    return "{\n  \"schema_version\": 1,\n  \"work_wait_ms\": " + std::to_string(value.workWaitMs) +
        ",\n  \"history_enabled\": " + (value.historyEnabled ? "true" : "false") + "\n}\n";
}

inline bool parseRuntimePreferences(const std::string &text, RuntimePreferences *out, std::string *error) {
    auto fail = [&](const char *reason) { if (error) *error = reason; return false; };
    JsonValue root;
    if (!parseJsonStrict(text, &root, nullptr)) return fail("invalid runtime preferences JSON; no values applied");
    uint64_t version = 0;
    const auto *schema = root.find("schema_version");
    if (!root.isObject() || !schema || !schema->asUint64(&version) || version != 1) return fail("unsupported runtime preferences schema");
    for (const auto &item : root.members)
        if (item.first != "schema_version" && item.first != "work_wait_ms" && item.first != "history_enabled") return fail("unknown runtime preference");
    RuntimePreferences value;
    if (const auto *wait = root.find("work_wait_ms")) {
        uint64_t number = 0;
        if (!wait->asUint64(&number) || number > kMaxWorkWaitMs) return fail("work wait must be an integer from 0 to 1000 ms");
        value.workWaitMs = static_cast<unsigned>(number);
    }
    if (const auto *history = root.find("history_enabled")) {
        if (!history->isBool()) return fail("history_enabled must be true or false");
        value.historyEnabled = history->boolean;
    }
    *out = value;
    return true;
}

// OS-owned lock releases on process exit; never removes or steals another process's lock.
class RuntimePreferencesLease {
   public:
    RuntimePreferencesLease() = default;
    RuntimePreferencesLease(const RuntimePreferencesLease &) = delete;
    RuntimePreferencesLease &operator=(const RuntimePreferencesLease &) = delete;
    ~RuntimePreferencesLease() {
#if defined(_WIN32)
        if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
#else
        if (file_ >= 0) ::close(file_);
#endif
    }
    bool acquire(const std::filesystem::path &path) {
#if defined(_WIN32)
        file_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        BY_HANDLE_FILE_INFORMATION info{};
        return file_ != INVALID_HANDLE_VALUE && GetFileType(file_) == FILE_TYPE_DISK && GetFileInformationByHandle(file_, &info) &&
            !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
#else
        file_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        struct stat info{};
        return file_ >= 0 && fstat(file_, &info) == 0 && S_ISREG(info.st_mode) && flock(file_, LOCK_EX | LOCK_NB) == 0;
#endif
    }
   private:
#if defined(_WIN32)
    HANDLE file_ = INVALID_HANDLE_VALUE;
#else
    int file_ = -1;
#endif
};

class RuntimePreferencesStore {
   public:
    explicit RuntimePreferencesStore(std::filesystem::path path) : path_(std::move(path)) {}
    bool load(RuntimePreferences *out, std::string *error) {
        std::string text;
        bool exists = false;
        if (!read(&text, &exists, error)) return false;
        RuntimePreferences parsed;
        if (exists && !parseRuntimePreferences(text, &parsed, error)) return false;
        original_ = text;
        existed_ = exists;
        loaded_ = true;
        *out = parsed;
        return true;
    }
    bool save(const RuntimePreferences &value, std::string *error) {
        if (!loaded_ || value.workWaitMs > kMaxWorkWaitMs) return fail(error, "runtime preferences not loaded or invalid; not saved");
        std::error_code ec;
        if (path_.has_parent_path()) std::filesystem::create_directories(path_.parent_path(), ec);
        if (ec) return fail(error, "cannot create runtime preferences directory");
        auto lockPath = path_;
        lockPath += ".lock";
        RuntimePreferencesLease lease;
        if (!lease.acquire(lockPath)) return fail(error, "runtime preferences locked by another writer or invalid lock file");
        std::string current;
        bool exists = false;
        if (!read(&current, &exists, error)) return false;
        if (exists != existed_ || current != original_) return fail(error, "runtime preferences changed externally; reopen runtime controls before saving");
        static std::atomic<unsigned> serial{0};
        auto temporary = path_;
#if defined(_WIN32)
        const auto pid = GetCurrentProcessId();
#else
        const auto pid = getpid();
#endif
        temporary += ".tmp-" + std::to_string(pid) + "-" + std::to_string(++serial);
        int fd = -1;
#if defined(_WIN32)
        if (_wsopen_s(&fd, temporary.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT,
                      _SH_DENYRW, _S_IREAD | _S_IWRITE) != 0) return fail(error, "cannot create temporary runtime preferences");
        FILE *file = _fdopen(fd, "wb");
#else
        fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0) return fail(error, "cannot create temporary runtime preferences");
        FILE *file = fdopen(fd, "wb");
#endif
        if (!file) {
#if defined(_WIN32)
            _close(fd);
#else
            ::close(fd);
#endif
            std::filesystem::remove(temporary, ec);
            return fail(error, "cannot open temporary runtime preferences");
        }
        const auto text = runtimePreferencesJson(value);
        bool ok = fwrite(text.data(), 1, text.size(), file) == text.size() && fflush(file) == 0;
#if defined(_WIN32)
        ok = (_commit(fd) == 0) && ok;
#else
        ok = (fsync(fd) == 0) && ok;
#endif
        ok = (fclose(file) == 0) && ok;
        if (ok) {
#if defined(_WIN32)
            ok = MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
            std::filesystem::rename(temporary, path_, ec);
            ok = !ec;
#endif
        }
        if (!ok) {
            std::filesystem::remove(temporary, ec);
            return fail(error, "cannot replace runtime preferences; active values unchanged");
        }
        original_ = text;
        existed_ = true;
        return true;
    }
   private:
    std::filesystem::path path_;
    std::string original_;
    bool loaded_ = false, existed_ = false;
    static bool fail(std::string *error, const char *reason) { if (error) *error = reason; return false; }
    bool read(std::string *text, bool *exists, std::string *error) const {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(path_, ec);
        if (ec == std::errc::no_such_file_or_directory || (!ec && status.type() == std::filesystem::file_type::not_found)) {
            text->clear(); *exists = false; return true;
        }
        if (ec || !std::filesystem::is_regular_file(status)) return fail(error, "runtime preferences must be a readable regular file");
        std::ifstream file(path_, std::ios::binary);
        if (!file) return fail(error, "cannot read runtime preferences");
        char buffer[4097];
        file.read(buffer, sizeof(buffer));
        if (file.gcount() > 4096 || file.bad()) return fail(error, "runtime preferences too large or unreadable");
        text->assign(buffer, static_cast<size_t>(file.gcount())); *exists = true; return true;
    }
};

}  // namespace om
