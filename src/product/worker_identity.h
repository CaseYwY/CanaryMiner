// One-shot cancellable helper-side OS entropy; keys never travel in argv, environment or public status.
#pragma once
#include "local_task_process.h"
#include <vector>
namespace om {
struct WorkerIdentityResult {LocalTaskStatus status=LocalTaskStatus::Unavailable;std::vector<std::string> keys;};
class WorkerIdentitySource {
   public:
    explicit WorkerIdentitySource(std::filesystem::path helper):process_(std::move(helper)){}
    WorkerIdentityResult request(unsigned count,const std::atomic<bool> &cancel,unsigned timeoutMs=2000); // background owner only
   private:
    LocalTaskProcess process_;bool attempted_=false; // never replace an unresolved entropy operation
};
bool workerIdentityRequest(const std::string &,uint64_t *parent,unsigned *count);
} // namespace om
