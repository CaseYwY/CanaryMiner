// Explicit injected development transport. No CLI/default factory, credentials discovery, fallback or donation authority.
#pragma once
#include "duplex_task_process.h"
#include "pool_tls_rpc.h"
#include <csignal>
namespace om {
class PoolTlsProcessTransport final:public PearlPoolTransport {
   public:
    PoolTlsProcessTransport(std::filesystem::path helper,PoolTlsCheckRequest,const std::atomic<bool> &cancel,
                            const volatile sig_atomic_t *stop=nullptr,
                            PoolTlsTransportMode mode=PoolTlsTransportMode::Tls);
    ~PoolTlsProcessTransport()override;
    PearlPoolConnectStatus open(const std::string &,int)override;
    PearlPoolConnectStatus openUntil(const std::string &,int,uint64_t originalDeadlineMonoMs);
    bool isOpen()const override;
    void close()override;
    bool progressIO()const override{return true;}
    bool bufferedRead()const override;
    bool localReadPending()const override;
    PearlPoolWaitStatus waitLocalRead(int)override;
    bool setNonblocking(bool)override;
    PearlPoolWaitStatus waitReadable(int ms)override{return waitFor(PearlPoolInterest::Read,ms);}
    PearlPoolWaitStatus waitWritable(int ms)override{return waitFor(PearlPoolInterest::Write,ms);}
    PearlPoolWaitStatus waitFor(PearlPoolInterest,int)override;
    PearlPoolTransfer send(const char *,std::size_t)override;
    PearlPoolTransfer receive(char *,std::size_t)override;
    PearlTlsError error()const;
    std::optional<PoolTlsCheckReply> openFailure()const;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace om
