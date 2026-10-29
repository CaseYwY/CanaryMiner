// Exclusive local-file lease shared by independent stores. No data format, counters or financial interpretation.
#pragma once
#include <filesystem>
#include <optional>
#include <cerrno>
#include "../core/platform.h"
#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#endif

namespace om {
// Helper-side non-creating probe. nullopt means unavailable, never evidence of a live owner.
inline std::optional<bool> localStoreLeaseHeld(const std::filesystem::path &path){
#if defined(_WIN32)
    const auto handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_SHARING_VIOLATION)return true;return {};}
    BY_HANDLE_FILE_INFORMATION info{};const bool regular=GetFileType(handle)==FILE_TYPE_DISK&&GetFileInformationByHandle(handle,&info)&&
        !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));CloseHandle(handle);
    return regular?std::optional<bool>(false):std::optional<bool>{};
#else
    const int fd=::open(path.c_str(),O_RDWR|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return {};
    struct stat info{};std::optional<bool> held;
    if(fstat(fd,&info)==0&&S_ISREG(info.st_mode)){
        if(flock(fd,LOCK_EX|LOCK_NB)==0)held=false;
        else if(errno==EWOULDBLOCK||errno==EAGAIN)held=true;
    }
    ::close(fd);return held;
#endif
}
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
