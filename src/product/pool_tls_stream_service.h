#pragma once
#include "pool_tls_connection.h"
#include "pool_tls_rpc.h"
namespace om {
using PoolTlsOpenFactory=std::function<PoolTlsOpenResult(const PoolTlsCheckRequest &,const std::atomic<bool> &)>;
// Native helper main only. The synthetic factory is supplied only by separate test executables.
int runPoolTlsStreamService(int argc,char **argv,const PoolTlsOpenFactory &,
                            const PoolTlsOpenFactory &plaintextFactory={});
} // namespace om
