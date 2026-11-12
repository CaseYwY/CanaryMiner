// Helper-only retained native connection. A live object, never a transferable check receipt, owns authentication.
#pragma once
#include "pool_tls_check.h"
#include "pool_tls_transport_state.h"
namespace om {
class PoolTlsConnection {
   public:
    virtual ~PoolTlsConnection()=default;
    virtual PearlPoolTransfer send(const char *,std::size_t)=0;
    virtual PearlPoolTransfer receive(char *,std::size_t)=0;
    virtual PearlPoolWaitStatus waitFor(PearlPoolInterest,unsigned)=0;
    virtual bool bufferedRead()const=0;
    virtual PoolTlsRpcState state()const=0;
    virtual PearlTlsError error()const=0;
    virtual void abort()=0;
};
struct PoolTlsOpenResult {std::unique_ptr<PoolTlsConnection> connection;std::optional<PoolTlsCheckReply> failure;};
// Native helper owner only. cancel must outlive the returned connection/context and never be reset for another session.
PoolTlsOpenResult openPoolTlsConnection(const PoolTlsCheckRequest &,const std::atomic<bool> &cancel);
PoolTlsOpenResult openPoolPlaintextConnection(const PoolTlsCheckRequest &,const std::atomic<bool> &cancel);
} // namespace om
