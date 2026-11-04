// Local declarations only. No endpoint discovery, socket, TLS factory, credential lookup or mining authority.
#pragma once
#include "runtime_preferences.h"
#include "local_preferences_store.h"
#include "../core/http.h"
#include <vector>
namespace om {
enum class PoolSecurityMode { Unchanged, StrictTls, PreferTls };
enum class PoolSecurityScope { User, Donation };
struct PoolSecurityEndpoint {std::string host;unsigned port=0;bool tls=false;};
inline bool parsePoolSecurityUrl(const std::string &url,PoolSecurityEndpoint *out){
    if(!out||url.size()>300)return false;
    PoolSecurityEndpoint endpoint;std::size_t begin=0;
    if(url.rfind("stratum+ssl://",0)==0){endpoint.tls=true;begin=14;}
    else if(url.rfind("stratum+tcp://",0)==0)begin=14;else return false;
    const auto colon=url.find(':',begin);if(colon==url.npos||colon==begin||colon-begin>253||colon+1==url.size())return false;
    endpoint.host=url.substr(begin,colon-begin);bool numeric=true;std::size_t label=0;
    for(std::size_t i=0;i<=endpoint.host.size();++i){
        if(i==endpoint.host.size()||endpoint.host[i]=='.'){
            if(i==label||i-label>63||endpoint.host[label]=='-'||endpoint.host[i-1]=='-')return false;
            label=i+1;continue;}
        auto &ch=endpoint.host[i];if(ch>='A'&&ch<='Z')ch=static_cast<char>(ch-'A'+'a');
        if(!((ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='-'))return false;
        if(ch<'0'||ch>'9')numeric=false;
    }
    // This declaration slice is DNS-only: literal identity needs a separately qualified native IP policy.
    if(numeric)return false;
    const auto digits=url.size()-colon-1;if(digits>5||(digits>1&&url[colon+1]=='0'))return false;
    for(std::size_t i=colon+1;i<url.size();++i){const auto ch=url[i];if(ch<'0'||ch>'9')return false;endpoint.port=endpoint.port*10+static_cast<unsigned>(ch-'0');}
    if(!endpoint.port||endpoint.port>65535)return false;
    *out=std::move(endpoint);return true;
}
inline std::string poolSecurityUrl(const PoolSecurityEndpoint &v){return std::string(v.tls?"stratum+ssl://":"stratum+tcp://")+v.host+":"+std::to_string(v.port);}
struct PoolSecurityRule {
    PoolSecurityMode mode=PoolSecurityMode::Unchanged;
    std::string tlsUrl,plaintextUrl;
    bool plaintextConsent=false;
    bool operator==(const PoolSecurityRule &v)const{return mode==v.mode&&tlsUrl==v.tlsUrl&&plaintextUrl==v.plaintextUrl&&plaintextConsent==v.plaintextConsent;}
    bool operator!=(const PoolSecurityRule &v)const{return !(*this==v);}
};
struct PoolSecurityPreferences {
    std::vector<PoolSecurityRule> users={PoolSecurityRule{}};
    PoolSecurityRule donation;
    bool operator==(const PoolSecurityPreferences &v)const{return users==v.users&&donation==v.donation;}
    bool operator!=(const PoolSecurityPreferences &v)const{return !(*this==v);}
};
inline const char *poolSecurityModeName(PoolSecurityMode v){return v==PoolSecurityMode::Unchanged?"unchanged":v==PoolSecurityMode::StrictTls?"strict_tls":v==PoolSecurityMode::PreferTls?"prefer_tls":"invalid";}
inline bool validPoolSecurityRule(const PoolSecurityRule &v,std::string *error=nullptr,unsigned *field=nullptr){
    const auto fail=[&](unsigned n,const char *message){if(field)*field=n;if(error)*error=message;return false;};
    if(v.mode!=PoolSecurityMode::Unchanged&&v.mode!=PoolSecurityMode::StrictTls&&v.mode!=PoolSecurityMode::PreferTls)return fail(0,"Choose unchanged, strict TLS or prefer TLS.");
    PoolSecurityEndpoint endpoint;
    if(!v.tlsUrl.empty()&&(!parsePoolSecurityUrl(v.tlsUrl,&endpoint)||!endpoint.tls||poolSecurityUrl(endpoint)!=v.tlsUrl))return fail(1,"TLS URL must be canonical stratum+ssl://DNS:PORT; no credentials, paths, IPs or inferred ports.");
    if(v.mode!=PoolSecurityMode::Unchanged&&v.tlsUrl.empty())return fail(1,"An explicit TLS endpoint is required; no port or pool is guessed.");
    if(!v.plaintextUrl.empty()&&(!parsePoolSecurityUrl(v.plaintextUrl,&endpoint)||endpoint.tls||poolSecurityUrl(endpoint)!=v.plaintextUrl))return fail(2,"Plaintext URL must be canonical stratum+tcp://DNS:PORT; no inferred endpoint.");
    if(v.plaintextConsent&&(v.mode!=PoolSecurityMode::PreferTls||v.plaintextUrl.empty()))return fail(3,"Plaintext consent requires prefer TLS and an independently configured TCP endpoint.");
    return true;
}
inline bool validPoolSecurityPreferences(const PoolSecurityPreferences &v,std::string *error=nullptr){
    if(v.users.empty()||v.users.size()>8){if(error)*error="Configure one to eight explicit user-pool slots.";return false;}
    for(const auto &rule:v.users)if(!validPoolSecurityRule(rule,error))return false;
    return validPoolSecurityRule(v.donation,error);
}
inline bool poolSecurityRequested(const PoolSecurityPreferences &v){
    if(v.donation.mode!=PoolSecurityMode::Unchanged)return true;
    for(const auto &r:v.users)if(r.mode!=PoolSecurityMode::Unchanged)return true;
    return false;
}
inline std::string poolSecurityRuleJson(const PoolSecurityRule &v){return "{\"mode\":"+jsonQuoted(poolSecurityModeName(v.mode))+",\"tls_url\":"+jsonQuoted(v.tlsUrl)+
    ",\"plaintext_url\":"+jsonQuoted(v.plaintextUrl)+",\"plaintext_consent\":"+(v.plaintextConsent?"true":"false")+"}";}
inline std::string poolSecurityPreferencesJson(const PoolSecurityPreferences &v){
    std::string result="{\"schema_version\":1,\"user_pools\":[";for(std::size_t i=0;i<v.users.size();++i){if(i)result+=',';result+=poolSecurityRuleJson(v.users[i]);}
    return result+"],\"donation\":"+poolSecurityRuleJson(v.donation)+"}\n";
}
inline bool parsePoolSecurityPreferences(const std::string &text,PoolSecurityPreferences *out,std::string *error){
    const auto fail=[&](const char *message){if(error)*error=message;return false;};JsonValue root;uint64_t version=0;
    if(!out||text.size()>8192||!parseJsonStrict(text,&root,nullptr)||!root.isObject()||root.members.size()!=3)return fail("Invalid pool-security preferences; original preserved.");
    const auto *schema=root.find("schema_version"),*users=root.find("user_pools"),*donation=root.find("donation");
    if(!schema||!schema->asUint64(&version)||version!=1)return fail("Unsupported pool-security preferences schema; preserved.");
    if(!users||!users->isArray()||users->items.empty()||users->items.size()>8||!donation)return fail("Missing or invalid independent pool scopes.");
    const auto decode=[&](const JsonValue &json,PoolSecurityRule &v){
        if(!json.isObject()||json.members.size()!=4)return false;
        const auto *mode=json.find("mode"),*tls=json.find("tls_url"),*tcp=json.find("plaintext_url"),*consent=json.find("plaintext_consent");
        if(!mode||!mode->isString()||!tls||!tls->isString()||!tcp||!tcp->isString()||!consent||!consent->isBool())return false;
        if(mode->text=="unchanged")v.mode=PoolSecurityMode::Unchanged;else if(mode->text=="strict_tls")v.mode=PoolSecurityMode::StrictTls;
        else if(mode->text=="prefer_tls")v.mode=PoolSecurityMode::PreferTls;else return false;
        v.tlsUrl=tls->text;v.plaintextUrl=tcp->text;v.plaintextConsent=consent->boolean;return validPoolSecurityRule(v,error);
    };
    PoolSecurityPreferences value;value.users.clear();
    for(const auto &item:users->items){PoolSecurityRule rule;if(!decode(item,rule))return fail("Invalid user-pool declaration; no values applied.");value.users.push_back(std::move(rule));}
    if(!decode(*donation,value.donation))return fail("Invalid independent donation declaration; no values applied.");
    *out=std::move(value);return true;
}
using PoolSecurityPreferencesStore=LocalPreferencesStore<PoolSecurityPreferences,parsePoolSecurityPreferences,poolSecurityPreferencesJson,validPoolSecurityPreferences>;
// This gate is bookkeeping, not a connection factory. Failure evidence must belong to this exact frozen attempt.
enum class PoolTlsFailure { Dns, Connect, NetworkTimeout, Verification, Protocol, Unknown, Cancelled, Clock, Unavailable };
enum class PoolFallbackPhase { Unknown, Resolve, Connect, Handshake, Authenticated };
struct PoolFallbackFailure {
    uint64_t attempt=0;PoolSecurityScope scope=PoolSecurityScope::User;std::size_t userSlot=0;
    std::string tlsUrl;PoolTlsFailure failure=PoolTlsFailure::Unknown;
    bool explicitStrictUrl=false,verified=false,applicationAttempted=false;
    PoolFallbackPhase phase=PoolFallbackPhase::Unknown; // a generic helper/trust/handshake timeout is NOT network evidence
};
class PoolFallbackGate {
   public:
    PoolFallbackGate(PoolSecurityPreferences preferences,PoolSecurityScope scope,std::size_t slot,uint64_t attempt):preferences_(std::move(preferences)),scope_(scope),slot_(slot),attempt_(attempt){
        if(!attempt||!validPoolSecurityPreferences(preferences_)||static_cast<unsigned>(scope)>1||(scope==PoolSecurityScope::User&&slot>=preferences_.users.size())||(scope==PoolSecurityScope::Donation&&slot))stopped_=true;
    }
    std::optional<PoolSecurityEndpoint> select(const PoolFallbackFailure &e){
        if(stopped_)return {};
        stopped_=true;
        if(e.attempt!=attempt_||e.scope!=scope_||e.userSlot!=slot_||e.explicitStrictUrl||e.verified||e.applicationAttempted)return {};
        // No independently qualified secure donation endpoint exists in this build. Declarations cannot
        // invent one or select a replacement fee peer/account; separate consent alone is not capability.
        if(scope_==PoolSecurityScope::Donation)return {};
        if((e.failure==PoolTlsFailure::Dns&&e.phase!=PoolFallbackPhase::Resolve)||
            (e.failure==PoolTlsFailure::Connect&&e.phase!=PoolFallbackPhase::Connect)||
            (e.failure==PoolTlsFailure::NetworkTimeout&&e.phase!=PoolFallbackPhase::Resolve&&e.phase!=PoolFallbackPhase::Connect))return {};
        const auto &rule=scope_==PoolSecurityScope::Donation?preferences_.donation:preferences_.users[slot_];
        if(rule.mode!=PoolSecurityMode::PreferTls||!rule.plaintextConsent||e.tlsUrl!=rule.tlsUrl||
            (e.failure!=PoolTlsFailure::Dns&&e.failure!=PoolTlsFailure::Connect&&e.failure!=PoolTlsFailure::NetworkTimeout))return {};
        PoolSecurityEndpoint result;if(!parsePoolSecurityUrl(rule.plaintextUrl,&result)||result.tls)return {};return result;
    }
    void stop(){stopped_=true;}
   private:
    PoolSecurityPreferences preferences_;PoolSecurityScope scope_;std::size_t slot_;uint64_t attempt_;bool stopped_=false;
};
} // namespace om
