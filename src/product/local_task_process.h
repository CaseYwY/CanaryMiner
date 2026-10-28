// Bounded one-shot installed native helper. Local IPC only; no shell, provider executable or mining control.
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace om {
constexpr size_t localTaskRequestLimit = 256 * 1024, localTaskResponseLimit = 64 * 1024;
enum class LocalTaskStatus { Ok, Invalid, Busy, Unavailable, Cancelled, Timeout, Unreaped };
struct LocalTaskResult {
    LocalTaskStatus status = LocalTaskStatus::Unavailable;
    bool childStarted = false;
    std::string response;
};
bool localTaskPath(const std::filesystem::path &path);  // lexical only; protected local installation/parent required
class LocalTaskProcess {
   public:
    explicit LocalTaskProcess(std::filesystem::path installedHelper);
    ~LocalTaskProcess();
    LocalTaskProcess(const LocalTaskProcess &) = delete;
    LocalTaskProcess &operator=(const LocalTaskProcess &) = delete;
    LocalTaskResult run(const std::string &request, const std::atomic<bool> &cancel, unsigned timeoutMs);
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Helper-side entry only. Input cannot be an arbitrary command; the caller decodes its own strict versioned contract.
bool readLocalTaskInput(int argc, char **argv, std::string *request);
bool writeLocalTaskOutput(const std::string &response);
}  // namespace om
