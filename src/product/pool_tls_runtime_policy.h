// Ordinary first-user-pool strict TLS admission. No network, trust import, fallback or donation authority.
#pragma once
#include "config.h"
#include "pool_security_preferences.h"

namespace om {
inline constexpr bool poolTlsRuntimeAvailable(){
#if defined(CANARY_ENABLE_POOL_TLS_RUNTIME) && CANARY_ENABLE_POOL_TLS_RUNTIME
    return true;
#else
    return false;
#endif
}
struct PoolTlsRuntimePolicy {
    bool tls=false;
    std::string host;
    unsigned port=0;
    std::string frozenUrl;
};

inline bool resolvePoolTlsRuntimePolicy(const ProductConfig &config,const PoolSecurityPreferences &saved,
                                       bool ordinary,bool available,PoolTlsRuntimePolicy *out,std::string *error){
    const auto fail=[&](const char *text){if(error)*error=text;return false;};
    if(!out||!validPoolSecurityPreferences(saved))return fail("pool TLS settings are invalid; no connection or mining was started");
    if(saved.donation.mode!=PoolSecurityMode::Unchanged)
        return fail("donation TLS is not supported by this runtime; user-pool permission cannot authorize a donation peer; no mining was started");
    for(size_t i=0;i<saved.users.size();++i){
        if(saved.users[i].mode==PoolSecurityMode::PreferTls)
            return fail("prefer-TLS and plaintext fallback are not supported by this runtime; choose strict TLS; no mining was started");
        if(i&&saved.users[i].mode!=PoolSecurityMode::Unchanged)
            return fail("pool TLS runtime supports only the first user pool; unused secure slots cannot be activated; no mining was started");
    }
    for(size_t i=1;i<config.pools.size();++i)if(config.pools[i].tls)
        return fail("pool TLS runtime supports only the first user pool; an unused strict TLS URL is refused; no mining was started");
    const auto &rule=saved.users.front();
    const bool explicitTls=!config.pools.empty()&&config.pools.front().tls;
    const bool requested=explicitTls||rule.mode==PoolSecurityMode::StrictTls;
    PoolTlsRuntimePolicy policy;
    if(!requested){*out=std::move(policy);return true;}
    if(!ordinary)return fail("strict pool TLS is supported only for ordinary user-pool mining; research and evidence modes are refused before device setup");
    if(!available)return fail("strict pool TLS runtime is unavailable in this build; no TCP fallback or mining was started");
    if(config.donation)return fail("strict user-pool TLS requires --no-donation or donation:false; donation transport is not enabled; no mining was started");
    if(config.pools.empty())return fail("strict TLS requires a configured first user pool; no peer or port is guessed; no mining was started");
    policy.host=config.pools.front().host;
    if(!normalizePoolTlsDns(&policy.host))return fail("strict pool TLS requires an ASCII DNS identity; no peer substitution or mining was started");
    policy.port=static_cast<unsigned>(config.pools.front().port);
    if(rule.mode==PoolSecurityMode::StrictTls){
        PoolSecurityEndpoint declared;
        if(!parsePoolSecurityUrl(rule.tlsUrl,&declared)||!declared.tls||declared.host!=policy.host)
            return fail("saved strict TLS endpoint must match the first pool DNS host; credentials cannot be redirected; no mining was started");
        if(explicitTls&&declared.port!=policy.port)
            return fail("explicit and saved strict TLS endpoints conflict; choose one matching endpoint before mining");
        policy.port=declared.port;
    }
    if(!policy.port||policy.port>65535)return fail("strict pool TLS requires an explicit valid port; no mining was started");
    policy.tls=true;policy.frozenUrl="stratum+ssl://"+policy.host+":"+std::to_string(policy.port);
    *out=std::move(policy);return true;
}

// A restarted worker may re-read its original config, but cannot switch the parent's secure peer or donation choice.
inline bool applyFrozenPoolTlsEndpoint(const std::string &url,ProductConfig *config){
    if(!config||config->donation||config->pools.empty())return false;
    PoolSecurityEndpoint endpoint;
    if(!parsePoolSecurityUrl(url,&endpoint)||!endpoint.tls||poolSecurityUrl(endpoint)!=url)return false;
    std::string host=config->pools.front().host;
    if(!normalizePoolTlsDns(&host)||host!=endpoint.host)return false;
    auto &pool=config->pools.front();pool.host=endpoint.host;pool.port=static_cast<int>(endpoint.port);pool.tls=true;
    return true;
}
} // namespace om
