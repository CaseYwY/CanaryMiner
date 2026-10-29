// Cooperative whole-collector exclusion plus transaction-time fencing. Protected local directories are required.
#pragma once
#include "energy_authorization.h"
namespace om {
struct EnergyCollectorLeaseRequest {uint64_t parentPid=0;EnergyAuthorizationRequest provision;};
struct EnergyCollectorLeaseReply {EnergyAuthorizationReply provision;std::string epoch;};
struct EnergyCollectorGuardRequest {uint64_t parentPid=0;EnergyCollectorFence fence;EnergyHistoryRequest history;};
bool encodeEnergyCollectorLeaseRequest(const EnergyCollectorLeaseRequest &,std::string *wire);
bool decodeEnergyCollectorLeaseRequest(const std::string &wire,EnergyCollectorLeaseRequest *);
bool encodeEnergyCollectorLeaseReply(const EnergyCollectorLeaseRequest &,const EnergyCollectorLeaseReply &,std::string *wire);
bool decodeEnergyCollectorLeaseReply(const EnergyCollectorLeaseRequest &,const std::string &wire,EnergyCollectorLeaseReply *);
bool encodeEnergyCollectorGuardRequest(const EnergyCollectorGuardRequest &,std::string *wire);
bool decodeEnergyCollectorGuardRequest(const std::string &wire,EnergyCollectorGuardRequest *);
int serveEnergyCollectorLease(const EnergyCollectorLeaseRequest &); // helper holds lifecycle lease until owned process termination
EnergyHistoryReply executeEnergyCollectorGuard(const EnergyCollectorGuardRequest &); // helper checks fence under operation lock
} // namespace om
