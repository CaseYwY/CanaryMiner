// Optional OS-maintained SQLite C ABI. No SDK headers, downloaded DLLs or extension loading.
#pragma once

#include <cstdint>
#include <filesystem>

#include "../core/platform.h"
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace om {

struct SqliteDb;
struct SqliteStatement;

class SqliteRuntime {
   public:
    int (*openV2)(const char *, SqliteDb **, int, const char *) = nullptr;
    int (*closeV2)(SqliteDb *) = nullptr;
    int (*prepare)(SqliteDb *, const char *, int, SqliteStatement **, const char **) = nullptr;
    int (*bindInteger)(SqliteStatement *, int, int64_t) = nullptr;
    int (*bindText)(SqliteStatement *, int, const char *, int, void (*)(void *)) = nullptr;
    int (*step)(SqliteStatement *) = nullptr;
    int (*finalize)(SqliteStatement *) = nullptr;
    int64_t (*columnInteger)(SqliteStatement *, int) = nullptr;
    int (*columnType)(SqliteStatement *, int) = nullptr;
    const unsigned char *(*columnText)(SqliteStatement *, int) = nullptr;
    int (*columnBytes)(SqliteStatement *, int) = nullptr;
    int (*execute)(SqliteDb *, const char *, int (*)(void *, int, char **, char **), void *, char **) = nullptr;
    int (*busyTimeout)(SqliteDb *, int) = nullptr;
    int (*versionNumber)() = nullptr;
    int (*limit)(SqliteDb *, int, int) = nullptr;

    SqliteRuntime() = default;
    SqliteRuntime(const SqliteRuntime &) = delete;
    SqliteRuntime &operator=(const SqliteRuntime &) = delete;
    ~SqliteRuntime() {
#if defined(_WIN32)
        if (library_) FreeLibrary(library_);
#else
        if (library_) dlclose(library_);
#endif
    }

    bool load() {
        if (library_) return loaded_;
#if defined(_WIN32)
        wchar_t directory[32768];
        const UINT length = GetSystemDirectoryW(directory, static_cast<UINT>(sizeof(directory) / sizeof(directory[0])));
        if (!length || length >= sizeof(directory) / sizeof(directory[0])) return false;
        const auto file = std::filesystem::path(directory) / L"winsqlite3.dll";
        library_ = LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
        library_ = dlopen("libsqlite3.so.0", RTLD_NOW | RTLD_LOCAL);
#endif
        if (!library_) return false;
        loaded_ = bind(openV2, "sqlite3_open_v2") && bind(closeV2, "sqlite3_close_v2") && bind(prepare, "sqlite3_prepare_v2") &&
               bind(bindInteger, "sqlite3_bind_int64") && bind(bindText, "sqlite3_bind_text") && bind(step, "sqlite3_step") && bind(finalize, "sqlite3_finalize") &&
               bind(columnInteger, "sqlite3_column_int64") && bind(columnType, "sqlite3_column_type") && bind(columnText, "sqlite3_column_text") && bind(columnBytes, "sqlite3_column_bytes") &&
               bind(execute, "sqlite3_exec") && bind(busyTimeout, "sqlite3_busy_timeout") && bind(versionNumber, "sqlite3_libversion_number") && bind(limit, "sqlite3_limit") &&
               versionNumber() >= 3031000;
        return loaded_;
    }

   private:
    bool loaded_ = false;
#if defined(_WIN32)
    HMODULE library_ = nullptr;
#else
    void *library_ = nullptr;
#endif
    template <typename T> bool bind(T &target, const char *name) {
#if defined(_WIN32)
        target = reinterpret_cast<T>(GetProcAddress(library_, name));
#else
        target = reinterpret_cast<T>(dlsym(library_, name));
#endif
        return target != nullptr;
    }
};

}  // namespace om
