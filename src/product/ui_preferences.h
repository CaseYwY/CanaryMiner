// Versioned local presentation preferences. Never rewrites mining config or stores wallets/tariffs.
#pragma once

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "../core/platform.h"
#if defined(_WIN32)
#include <windows.h>
#endif
#include "json_dom.h"

namespace om {

struct UiPreferences {
    unsigned refreshMs = 500;
    bool showEvents = true;
    bool hideWorker = false;
    bool color = true;  // false disables color; true still respects NO_COLOR and terminal capability

    bool operator==(const UiPreferences &other) const {
        return refreshMs == other.refreshMs && showEvents == other.showEvents && hideWorker == other.hideWorker && color == other.color;
    }
    bool operator!=(const UiPreferences &other) const { return !(*this == other); }
};

inline bool validUiPreferences(const UiPreferences &p) {
    return p.refreshMs == 100 || p.refreshMs == 250 || p.refreshMs == 500 || p.refreshMs == 1000;
}

inline std::string uiPreferencesJson(const UiPreferences &p) {
    return std::string("{\n  \"schema_version\": 1,\n  \"refresh_ms\": ") + std::to_string(p.refreshMs) +
           ",\n  \"show_events\": " + (p.showEvents ? "true" : "false") + ",\n  \"hide_worker\": " +
           (p.hideWorker ? "true" : "false") + ",\n  \"color\": " + (p.color ? "true" : "false") + "\n}\n";
}

inline bool parseUiPreferences(const std::string &text, UiPreferences *out, std::string *error) {
    JsonValue root;
    if (!parseJsonStrict(text, &root, error)) return false;
    auto fail = [&](const char *reason) { if (error) *error = reason; return false; };
    if (!root.isObject()) return fail("UI preferences must be an object");
    for (const auto &m : root.members)
        if (m.first != "schema_version" && m.first != "refresh_ms" && m.first != "show_events" && m.first != "hide_worker" && m.first != "color")
            return fail("unknown UI preference; no changes applied");
    uint64_t version = 0;
    const JsonValue *v = root.find("schema_version");
    if (!v || !v->asUint64(&version) || version != 1) return fail("unsupported UI preferences schema");
    UiPreferences result;
    if (const auto *value = root.find("refresh_ms")) {
        uint64_t n = 0;
        if (!value->asUint64(&n) || n > 1000) return fail("invalid display refresh interval");
        result.refreshMs = static_cast<unsigned>(n);
    }
    const std::pair<const char *, bool *> booleans[] = {{"show_events", &result.showEvents}, {"hide_worker", &result.hideWorker},
                                                       {"color", &result.color}};
    for (const auto &item : booleans) {
        if (const auto *value = root.find(item.first)) {
            if (!value->isBool()) return fail("UI switch must be true or false");
            *item.second = value->boolean;
        }
    }
    if (!validUiPreferences(result)) return fail("display refresh must be 100, 250, 500 or 1000 ms");
    *out = result;
    return true;
}

class UiPreferencesStore {
   public:
    explicit UiPreferencesStore(std::filesystem::path path) : path_(std::move(path)) {}

    bool load(UiPreferences *out, std::string *error) {
        std::string text;
        bool exists = false;
        if (!read(&text, &exists, error)) return false;
        UiPreferences parsed;
        if (exists && !parseUiPreferences(text, &parsed, error)) return false;
        original_ = text;
        existed_ = exists;
        loaded_ = true;
        *out = parsed;
        return true;
    }

    bool save(const UiPreferences &value, std::string *error) {
        if (!loaded_ || !validUiPreferences(value)) return fail(error, "preferences not loaded or invalid; not saved");
        std::error_code ec;
        if (path_.has_parent_path()) std::filesystem::create_directories(path_.parent_path(), ec);
        if (ec) return fail(error, "cannot create UI preferences directory");
        auto lockPath = path_;
        lockPath += ".lock";
        if (!std::filesystem::create_directory(lockPath, ec)) return fail(error, "UI preferences are locked; retry after the other writer finishes");
        struct Lock {
            std::filesystem::path path;
            ~Lock() { std::error_code ignored; std::filesystem::remove(path, ignored); }
        } lock{lockPath};
        std::string current;
        bool exists = false;
        if (!read(&current, &exists, error)) return false;
        if (exists != existed_ || current != original_) return fail(error, "UI preferences changed externally; reopen settings before saving");
        const std::string text = uiPreferencesJson(value);
        static std::atomic<unsigned> serial{0};
        auto temp = path_;
#if defined(_WIN32)
        const auto pid = GetCurrentProcessId();
#else
        const auto pid = getpid();
#endif
        temp += ".tmp-" + std::to_string(pid) + "-" + std::to_string(++serial);
        int fd = -1;
#if defined(_WIN32)
        if (_wsopen_s(&fd, temp.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _SH_DENYRW, _S_IREAD | _S_IWRITE) != 0)
            return fail(error, "cannot create temporary UI preferences");
        FILE *file = _fdopen(fd, "wb");
#else
        fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0) return fail(error, "cannot create temporary UI preferences");
        FILE *file = fdopen(fd, "wb");
#endif
        if (!file) {
#if defined(_WIN32)
            _close(fd);
#else
            ::close(fd);
#endif
            std::filesystem::remove(temp, ec);
            return fail(error, "cannot open temporary UI preferences");
        }
        bool ok = fwrite(text.data(), 1, text.size(), file) == text.size() && fflush(file) == 0;
#if defined(_WIN32)
        ok = (_commit(fd) == 0) && ok;
#else
        ok = (fsync(fd) == 0) && ok;
#endif
        ok = (fclose(file) == 0) && ok;
        if (ok) {
#if defined(_WIN32)
            ok = MoveFileExW(temp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
            std::filesystem::rename(temp, path_, ec);
            ok = !ec;
#endif
        }
        if (!ok) {
            std::filesystem::remove(temp, ec);
            return fail(error, "cannot replace UI preferences; active values unchanged");
        }
        original_ = text;
        existed_ = true;
        return true;
    }

   private:
    std::filesystem::path path_;
    std::string original_;
    bool existed_ = false, loaded_ = false;

    static bool fail(std::string *error, const char *text) { if (error) *error = text; return false; }
    bool read(std::string *text, bool *exists, std::string *error) const {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(path_, ec);
        if (ec == std::errc::no_such_file_or_directory || (!ec && status.type() == std::filesystem::file_type::not_found)) {
            *exists = false;
            text->clear();
            return true;
        }
        if (ec || !std::filesystem::is_regular_file(status)) return fail(error, "UI preferences must be a readable regular file");
        std::ifstream file(path_, std::ios::binary);
        if (!file) return fail(error, "cannot read UI preferences");
        char buffer[16385];
        file.read(buffer, sizeof(buffer));
        const auto count = file.gcount();
        if (count > 16384 || file.bad()) return fail(error, "UI preferences are too large or unreadable");
        text->assign(buffer, static_cast<size_t>(count));
        *exists = true;
        return true;
    }
};

}  // namespace om
