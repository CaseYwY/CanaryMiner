// Private owned duplex native helper channels. Single owner, bounded nonblocking IO, no shell or mining controls.
#pragma once
#include "local_task_process.h"
namespace om {
constexpr std::size_t duplexTaskFrameLimit=localTaskRequestLimit+264;
enum class DuplexTaskState { Dormant, Running, Exited, Failed, Stopped, Unreaped };
struct DuplexTaskPoll {
    DuplexTaskState state=DuplexTaskState::Dormant;
    bool flushed=false,eof=false;
    bool channelFault=false;
    int exitCode=-1;
    std::string bytes;
};
class DuplexTaskProcess {
   public:
    DuplexTaskProcess();~DuplexTaskProcess();
    DuplexTaskProcess(const DuplexTaskProcess &)=delete;DuplexTaskProcess &operator=(const DuplexTaskProcess &)=delete;
    bool start(const std::filesystem::path &,const std::atomic<bool> &cancel);
    bool send(const std::string &); // one immutable queued frame, never replaces a partial write
    DuplexTaskPoll poll(); // <= one 4096-byte read/write and native wait(0); no readiness wait
    void requestStop();
    bool terminate(unsigned reapMs=250); // only this owned helper; unresolved ownership is quarantined
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
enum class DuplexTaskIO { Bytes, Pending, Closed, Error };
struct DuplexTaskBytes {DuplexTaskIO status=DuplexTaskIO::Error;std::size_t bytes=0;};
class DuplexTaskChannel {
   public:
    DuplexTaskChannel();~DuplexTaskChannel();
    DuplexTaskChannel(const DuplexTaskChannel &)=delete;DuplexTaskChannel &operator=(const DuplexTaskChannel &)=delete;
    bool adopt(int argc,char **argv); // --local-stream-v1 plus inherited numeric handles/PID only
    bool parentAlive()const;
    uint64_t parentId()const;
    DuplexTaskBytes receive(char *,std::size_t);
    DuplexTaskBytes send(const char *,std::size_t);
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace om
