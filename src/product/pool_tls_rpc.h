// Private bounded duplex TLS RPC. No process/socket launch or ordinary-miner activation.
#pragma once
#include "pool_tls_check.h"
#include "pool_tls_transport_state.h"
#include <functional>
#include <string_view>
namespace om {
constexpr std::size_t poolTlsRpcPayloadLimit=16384,poolTlsRpcFrameLimit=localTaskRequestLimit+256;
enum class PoolTlsRpcOperation { Open, Write, Read, Wait, Abort, OpenPlaintext };
enum class PoolTlsRpcReady { NotWait, Ready, Timeout };
struct PoolTlsRpcCommand {
    uint64_t sequence=0,issuedMonoMs=0,deadlineMonoMs=0;
    PoolTlsRpcOperation operation=PoolTlsRpcOperation::Abort;
    std::size_t maximum=0;
    PearlPoolInterest interest=PearlPoolInterest::None;
    unsigned waitMs=0;
    std::string payload; // Open: exact frozen CMTLSQ1; Write: one stable owned plaintext chunk
};
struct PoolTlsRpcReply {
    uint64_t sequence=0;
    PearlPoolTransfer transfer;
    PoolTlsRpcState state=PoolTlsRpcState::Dormant;
    PearlTlsError error=PearlTlsError::None;
    std::string payload; // only a successful Read; never a native diagnostic or request echo
    PoolTlsRpcReady ready=PoolTlsRpcReady::NotWait;
    bool bufferedRead=false;
    std::optional<PoolTlsCheckReply> openFailure={}; // exact native Open failure only; never a parent timeout/IPC inference
};
PearlTlsError poolTlsRpcOpenError(const PoolTlsCheckReply &);
bool validPoolTlsRpcCommand(const PoolTlsRpcCommand &);
bool encodePoolTlsRpcCommand(const PoolTlsRpcCommand &,std::string *);
bool decodePoolTlsRpcCommand(std::string_view,PoolTlsRpcCommand *);
bool encodePoolTlsRpcReply(const PoolTlsRpcCommand &,const PoolTlsRpcReply &,std::string *);
bool decodePoolTlsRpcReply(const PoolTlsRpcCommand &,std::string_view,PoolTlsRpcReply *);
// Frames retain their original bytes across arbitrary local-pipe fragmentation. Failure latches without recovery/replay.
class PoolTlsRpcFramer {
   public:
    bool feed(std::string_view,const std::function<bool(std::string_view)> &);
    bool complete()const{return !failed_&&buffer_.empty()&&!expected_;}
    bool failed()const{return failed_;}
   private:
    std::string buffer_;std::size_t expected_=0;bool failed_=false;
};
// Parent-side one-flight command lifetime. Copies are bookkeeping, never share/pool acknowledgements.
class PoolTlsRpcGate {
   public:
    bool issue(const PoolTlsRpcCommand &,uint64_t nowMonoMs);
    bool settle(const PoolTlsRpcReply &,uint64_t nowMonoMs);
    void stop()noexcept;
    bool stopped()const{return stopped_;}
    const std::optional<PoolTlsRpcCommand> &pending()const{return pending_;}
    const std::optional<PoolTlsRpcReply> &settled()const{return settled_;}
   private:
    uint64_t sequence_=0,last_=0;bool attempted_=false,opened_=false,stopped_=false;
    std::optional<PoolTlsRpcCommand> pending_;
    std::optional<PoolTlsRpcReply> settled_;
    std::optional<std::string> retryWrite_;
    PoolTlsTransportMode mode_=PoolTlsTransportMode::Tls;
    bool time(uint64_t);
};
} // namespace om
