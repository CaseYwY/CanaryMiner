// Private canonical monetary model shared by the ledger and recovery store. Not a public IPC format.
#pragma once
#include "billing_ledger.h"
namespace om { namespace billing_detail {
constexpr int64_t lastUtc=253402300799999LL;
constexpr size_t bindingLimit=1024*1024,chunkLimit=128*1024,maxChunkIntervals=512;
bool instant(int64_t time);
bool boundedChunk(const BillingChunk &chunk);
BillingStatus validateBinding(const BillingBinding &binding,const BillingLimits &limits,int64_t nowUtcMs);
std::string bindingText(const BillingBinding &binding,const BillingLimits &limits);
std::string chunkText(const BillingChunk &chunk);
bool parseChunk(const std::string &text,const PowerEvidence &evidence,BillingChunk &out);
std::string stateText(const BillingSnapshot &snapshot);
bool parseBinding(const std::string &text,BillingBinding &binding,BillingLimits &limits);
bool parseState(const std::string &text,const BillingBinding &binding,const BillingLimits &limits,int64_t nowUtcMs,BillingSnapshot &snapshot);
} } // namespace om::billing_detail
