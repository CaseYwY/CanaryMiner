// Synthetic policy, frozen worker arguments and donation isolation. No device, network, helper or store access.
#include "../src/product/pool_tls_runtime_policy.h"
#include "../src/product/supervisor.h"
#include "../src/product/fee_session.h"
#include "product_test_util.h"
#include <algorithm>

using namespace om;
int main(){
    ProductConfig config;config.donation=false;
    PoolEntry pool;std::string error;
    CHECK(parsePoolSpec("stratum+ssl://POOL-fixture.invalid:443",&pool,&error));
    CHECK(pool.tls&&pool.host=="pool-fixture.invalid"&&pool.port==443);
    config.pools={pool};
    PoolSecurityPreferences saved;PoolTlsRuntimePolicy policy;
    CHECK(resolvePoolTlsRuntimePolicy(config,saved,true,true,&policy,&error));
    CHECK(policy.tls&&policy.host=="pool-fixture.invalid"&&policy.port==443&&policy.frozenUrl=="stratum+ssl://pool-fixture.invalid:443");
    const auto original=policy;
    CHECK(!resolvePoolTlsRuntimePolicy(config,saved,true,false,&policy,&error)&&error.find("no TCP fallback")!=error.npos);
    CHECK(policy.frozenUrl==original.frozenUrl&&policy.tls==original.tls);
    CHECK(!resolvePoolTlsRuntimePolicy(config,saved,false,true,&policy,&error));
    auto donated=config;donated.donation=true;
    CHECK(!resolvePoolTlsRuntimePolicy(donated,saved,true,true,&policy,&error)&&error.find("--no-donation")!=error.npos);
    CHECK(!resolvePoolTlsRuntimePolicy(config,saved,true,true,nullptr,&error));
    for(const auto &bad:std::vector<std::string>{"stratum+ssl://127.0.0.1:1","stratum+ssl://[::1]:1","stratum+ssl://pool_fixture.invalid:1",
        "stratum+ssl://pool..invalid:1","stratum+ssl://pool.invalid.:1","stratum+ssl://-pool.invalid:1","stratum+ssl://pool-.invalid:1",
        "stratum+ssl://user@pool.invalid:1","stratum+ssl://pool.invalid:1/path","stratum+ssl://pool.invalid:0","stratum+ssl://pool.invalid:65536",
        "stratum+ssl://stratum+tcp://pool.invalid:1","stratum+ssl://"+std::string(64,'a')+".invalid:1",std::string("stratum+ssl://pool\0.invalid:1",26)}){
        auto unchanged=pool;
        CHECK(!parsePoolSpec(bad,&unchanged,&error));
        CHECK(unchanged.host==pool.host&&unchanged.port==pool.port&&unchanged.tls);
    }
    CHECK(parsePoolSpec("stratum+tcp://pool-fixture.invalid:1",&config.pools[0],&error));
    CHECK(resolvePoolTlsRuntimePolicy(config,saved,false,false,&policy,&error)&&!policy.tls&&policy.frozenUrl.empty());
    saved.users[0]={PoolSecurityMode::StrictTls,"stratum+ssl://pool-fixture.invalid:443",{},false};
    CHECK(resolvePoolTlsRuntimePolicy(config,saved,true,true,&policy,&error)&&policy.tls&&policy.port==443);
    CHECK(config.pools[0].port==1&&!config.pools[0].tls); // pure resolution never mutates source configuration
    auto conflict=saved;conflict.users[0].tlsUrl="stratum+ssl://other-fixture.invalid:443";
    CHECK(!resolvePoolTlsRuntimePolicy(config,conflict,true,true,&policy,&error)&&error.find("credentials cannot be redirected")!=error.npos);
    auto explicitConfig=config;explicitConfig.pools[0]=pool;explicitConfig.pools[0].port=444;
    CHECK(!resolvePoolTlsRuntimePolicy(explicitConfig,saved,true,true,&policy,&error)&&error.find("conflict")!=error.npos);
    explicitConfig.pools[0].port=443;
    CHECK(resolvePoolTlsRuntimePolicy(explicitConfig,saved,true,true,&policy,&error));
    auto unavailable=saved;unavailable.users[0].mode=PoolSecurityMode::PreferTls;
    unavailable.users[0].plaintextUrl="stratum+tcp://pool-fixture.invalid:1";unavailable.users[0].plaintextConsent=true;
    CHECK(!resolvePoolTlsRuntimePolicy(config,unavailable,true,true,&policy,&error));
    unavailable=saved;unavailable.donation=saved.users[0];
    CHECK(!resolvePoolTlsRuntimePolicy(config,unavailable,true,true,&policy,&error));
    unavailable=saved;unavailable.users.push_back(saved.users[0]);
    CHECK(!resolvePoolTlsRuntimePolicy(config,unavailable,true,true,&policy,&error));
    unavailable=saved;unavailable.users.clear();
    CHECK(!resolvePoolTlsRuntimePolicy(config,unavailable,true,true,&policy,&error));
    auto multiple=config;multiple.pools.push_back(pool);
    CHECK(!resolvePoolTlsRuntimePolicy(multiple,saved,true,true,&policy,&error));
    multiple.pools[1].tls=false;
    CHECK(resolvePoolTlsRuntimePolicy(multiple,saved,true,true,&policy,&error)&&policy.tls);
    auto missing=config;missing.pools.clear();
    CHECK(!resolvePoolTlsRuntimePolicy(missing,saved,true,true,&policy,&error));

    // Parent-frozen TLS survives a changed TCP port; a different host or removed pool never inherits credentials.
    config.pools[0].account="fixture_account";config.pools[0].compression="off";
    config.pools[0].targetSemantics="final-adjusted-bound";
    const auto frozen=std::string("stratum+ssl://pool-fixture.invalid:443");
    CHECK(applyFrozenPoolTlsEndpoint(frozen,&config));
    CHECK(config.pools[0].tls&&config.pools[0].port==443&&config.pools[0].account=="fixture_account"&&
        config.pools[0].compression=="off"&&config.pools[0].targetSemantics=="final-adjusted-bound");
    for(unsigned mode=0;mode<4;++mode){
        auto changed=config;
        if(mode==0)changed.pools.clear();
        if(mode==1)changed.pools[0].host="other-fixture.invalid";
        if(mode==2)changed.donation=true;
        CHECK(!applyFrozenPoolTlsEndpoint(mode==3?"stratum+tcp://pool-fixture.invalid:443":frozen,&changed));
        CHECK(mode==0?changed.pools.empty():changed.pools[0].port==config.pools[0].port);
    }
    WorkerSeed seed;seed.index=2;seed.uuid="GPU-synthetic-fixture";
    const auto args=workerArgs({"run","--config","fixture.json","--worker-pool-tls","stratum+ssl://old.invalid:1","--work-wait-ms","4"},seed,9000,7,frozen);
    const auto found=std::find(args.begin(),args.end(),"--worker-pool-tls");
    CHECK(found!=args.end()&&found+1!=args.end()&&*(found+1)==frozen);
    CHECK(std::count(args.begin(),args.end(),"--worker-pool-tls")==1&&std::count(args.begin(),args.end(),"--no-donation")==1);
    CHECK(std::find(args.begin(),args.end(),"stratum+ssl://old.invalid:1")==args.end());
    CHECK(std::find(args.begin(),args.end(),"fixture.json")!=args.end());
    CHECK(effectiveConfigText(config,"fixture").find("stratum+ssl://pool-fixture.invalid:443")!=std::string::npos);

    // The early refusal must precede every scheduler clock, storage or run callback, even if miscalled internally.
    unsigned logs=0;FeeLoopDeps deps;deps.log=[&](const std::string &text){++logs;CHECK(text.find("refused")!=text.npos);};
    FeeSession fee(FeePolicy{},nullptr,FeeStateFile("unused-fixture-directory"),"synthetic-destination",deps);
    RunOptions options;options.poolTls=true;
    CHECK(fee.runFeeWindow(options)==kRunPauseFault&&logs==1);
    CHECK(fee.windowsCompleted()==0&&fee.poolFailures()==0);
    return finishTests("pool_tls_runtime_policy");
}
