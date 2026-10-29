// Bounded one-shot installed native helper. Local IPC only; no shell, provider executable or mining control.
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
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
uint64_t localTaskProcessId(); // ephemeral IPC only; never a durable device/session identity
bool bindLocalTaskParent(uint64_t expectedParent); // helper-side: Windows owned job / Linux parent-death signal
class LocalTaskProcess {
   public:
    explicit LocalTaskProcess(std::filesystem::path installedHelper);
    ~LocalTaskProcess();
    LocalTaskProcess(const LocalTaskProcess &) = delete;
    LocalTaskProcess &operator=(const LocalTaskProcess &) = delete;
    LocalTaskResult run(const std::string &request, const std::atomic<bool> &cancel, unsigned timeoutMs);
    // One bounded newline-terminated readiness frame; child must then remain alive and silent until cancellation.
    // Called only by an owned background thread. Unexpected exit (even code 0) is loss of the held lease, never success.
    LocalTaskResult hold(const std::string &request,const std::atomic<bool> &cancel,unsigned startupTimeoutMs,
                         const std::function<bool(const std::string &)> &ready);
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Helper-side entry only. Input cannot be an arbitrary command; the caller decodes its own strict versioned contract.
bool readLocalTaskInput(int argc, char **argv, std::string *request);
bool writeLocalTaskOutput(const std::string &response);
}  // namespace om
