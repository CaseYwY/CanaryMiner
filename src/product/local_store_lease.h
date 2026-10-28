// Exclusive local-file lease shared by independent stores. No data format, counters or financial interpretation.
#pragma once
#include <filesystem>
#include "../core/platform.h"
#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#endif

namespace om {
class LocalStoreLease {
   public:
    LocalStoreLease() = default;
    LocalStoreLease(const LocalStoreLease &) = delete;
    LocalStoreLease &operator=(const LocalStoreLease &) = delete;
    ~LocalStoreLease() {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
        if (fd_ >= 0) ::close(fd_);
#endif
    }
    bool acquire(const std::filesystem::path &path) {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE) return false;
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) return false;
        BY_HANDLE_FILE_INFORMATION info{};
        return GetFileType(handle_) == FILE_TYPE_DISK && GetFileInformationByHandle(handle_, &info) &&
            !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
#else
        if (fd_ >= 0) return false;
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        struct stat info {};
        return fd_ >= 0 && fstat(fd_, &info) == 0 && S_ISREG(info.st_mode) && flock(fd_, LOCK_EX | LOCK_NB) == 0;
#endif
    }
   private:
#if defined(_WIN32)
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};
}  // namespace om
