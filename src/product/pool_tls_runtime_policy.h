// Ordinary first-user-pool TLS admission and frozen fallback consent. No network or trust import.
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
    std::string fallbackUrl; // empty means no authority to send plaintext
};

inline bool resolvePoolTlsRuntimePolicy(const ProductConfig &config,const PoolSecurityPreferences &saved,
                                       bool ordinary,bool available,PoolTlsRuntimePolicy *out,std::string *error){
    const auto fail=[&](const char *text){if(error)*error=text;return false;};
    if(!out||!validPoolSecurityPreferences(saved))return fail("pool TLS settings are invalid; no connection or mining was started");
    if(saved.donation.mode!=PoolSecurityMode::Unchanged)
        return fail("donation TLS is not supported by this runtime; user-pool permission cannot authorize a donation peer; no mining was started");
    for(size_t i=0;i<saved.users.size();++i){
        if(i&&saved.users[i].mode!=PoolSecurityMode::Unchanged)
            return fail("pool TLS runtime supports only the first user pool; unused secure slots cannot be activated; no mining was started");
    }
    for(size_t i=1;i<config.pools.size();++i)if(config.pools[i].tls)
        return fail("pool TLS runtime supports only the first user pool; an unused strict TLS URL is refused; no mining was started");
    const auto &rule=saved.users.front();
    const bool explicitTls=!config.pools.empty()&&config.pools.front().tls;
    const bool requested=explicitTls||rule.mode!=PoolSecurityMode::Unchanged;
    PoolTlsRuntimePolicy policy;
    if(!requested){*out=std::move(policy);return true;}
    if(!ordinary)return fail("pool TLS is supported only for ordinary user-pool mining; research and evidence modes are refused before device setup");
    if(!available)return fail("pool TLS runtime is unavailable in this build; no TCP fallback or mining was started");
    if(config.donation)return fail("user-pool TLS requires --no-donation or donation:false; donation transport is not enabled; no mining was started");
    if(config.pools.empty())return fail("TLS requires a configured first user pool; no peer or port is guessed; no mining was started");
    policy.host=config.pools.front().host;
    if(!normalizePoolTlsDns(&policy.host))return fail("pool TLS requires an ASCII DNS identity; no peer substitution or mining was started");
    policy.port=static_cast<unsigned>(config.pools.front().port);
    if(rule.mode!=PoolSecurityMode::Unchanged){
        PoolSecurityEndpoint declared;
        if(!parsePoolSecurityUrl(rule.tlsUrl,&declared)||!declared.tls||declared.host!=policy.host)
            return fail("saved TLS endpoint must match the first pool DNS host; credentials cannot be redirected; no mining was started");
        if(explicitTls&&declared.port!=policy.port)
            return fail("explicit and saved TLS endpoints conflict; choose one matching endpoint before mining");
        policy.port=declared.port;
        if(rule.mode==PoolSecurityMode::PreferTls&&rule.plaintextConsent){
            PoolSecurityEndpoint plaintext;
            if(!parsePoolSecurityUrl(rule.plaintextUrl,&plaintext)||plaintext.tls||plaintext.host!=policy.host)
                return fail("consented plaintext endpoint must match the first pool DNS host; credentials cannot be redirected; no mining was started");
            // An explicit SSL URL is always strict, even if saved preferences allow fallback.
            if(!explicitTls)policy.fallbackUrl=rule.plaintextUrl;
        }
    }
    if(!policy.port||policy.port>65535)return fail("pool TLS requires an explicit valid port; no mining was started");
    policy.tls=true;policy.frozenUrl="stratum+ssl://"+policy.host+":"+std::to_string(policy.port);
    *out=std::move(policy);return true;
}

// A restarted worker may re-read its original config, but cannot switch the parent's secure peer or donation choice.
inline bool applyFrozenPoolTlsEndpoint(const std::string &url,ProductConfig *config,const std::string &fallbackUrl={}){
    if(!config||config->donation||config->pools.empty())return false;
    PoolSecurityEndpoint endpoint;
    if(!parsePoolSecurityUrl(url,&endpoint)||!endpoint.tls||poolSecurityUrl(endpoint)!=url)return false;
    std::string host=config->pools.front().host;
    if(!normalizePoolTlsDns(&host)||host!=endpoint.host)return false;
    if(!fallbackUrl.empty()){
        PoolSecurityEndpoint plaintext;
        // Check the source before replacing it: a new explicit SSL intent cannot inherit old fallback consent.
        if(config->pools.front().tls||!parsePoolSecurityUrl(fallbackUrl,&plaintext)||plaintext.tls||
           poolSecurityUrl(plaintext)!=fallbackUrl||plaintext.host!=endpoint.host)return false;
    }
    auto &pool=config->pools.front();pool.host=endpoint.host;pool.port=static_cast<int>(endpoint.port);pool.tls=true;
    return true;
}
} // namespace om
