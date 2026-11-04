// Explicit one-shot connection inspection, never a mining transport or endpoint/fallback selection policy.
#pragma once
#include "local_task_process.h"
#include "../core/pearl_tls_stream.h"
#include "../core/pearl_tls_private_trust.h"
#include <optional>
#include <utility>
namespace om {
struct PoolTlsCheckRequest {
    bool enabled=false;
    std::string host; // frozen original ASCII DNS reference AND resolver input; no separate substitute identity
    unsigned port=0,totalMs=5000;
    std::string caPem; // explicit Linux development anchors, no system-store installation
    std::shared_ptr<const PearlTlsPrivateTrustInput> windowsPrivateTrust;
};
enum class PoolTlsCheckStatus { Disabled, Unavailable, Invalid, Cancelled, Timeout, Dns, Socket, Connect, Tls, Verified };
enum class PoolTlsCheckStage { None, Resolve, Connect, Handshake, Complete };
struct PoolTlsCheckReply {
    PoolTlsCheckStatus status=PoolTlsCheckStatus::Unavailable;
    PoolTlsCheckStage stage=PoolTlsCheckStage::None;
    PearlTlsError tlsError=PearlTlsError::None;
};
bool validPoolTlsCheckRequest(const PoolTlsCheckRequest &);
bool encodePoolTlsCheckRequest(const PoolTlsCheckRequest &,uint64_t parent,std::string *);
bool decodePoolTlsCheckRequest(const std::string &,PoolTlsCheckRequest *,uint64_t *parent);
bool encodePoolTlsCheckReply(const PoolTlsCheckReply &,std::string *);
bool decodePoolTlsCheckReply(const std::string &,PoolTlsCheckReply *);
// Helper thread/process ONLY. Native resolver/trust/context destruction can block; parent deadline/termination is authoritative.
PoolTlsCheckReply executePoolTlsCheck(const PoolTlsCheckRequest &,const std::atomic<bool> &cancel);
struct PoolTlsCheckResult {
    LocalTaskStatus transport=LocalTaskStatus::Unavailable;
    bool childStarted=false;
    std::optional<PoolTlsCheckReply> reply;
};
class PoolTlsCheckClient {
   public:
    explicit PoolTlsCheckClient(std::filesystem::path helper):process_(std::move(helper)){}
    // Owned background thread only; join that caller before destruction. One object, one explicit attempt.
    // Cancel/timeout/late reply never retries, downgrades or revives this check.
    PoolTlsCheckResult inspect(const PoolTlsCheckRequest &,const std::atomic<bool> &cancel);
   private:
    LocalTaskProcess process_;std::atomic_flag attempted_=ATOMIC_FLAG_INIT;
};
} // namespace om
