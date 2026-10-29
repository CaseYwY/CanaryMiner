// Synthetic CPU-only collector inputs. These UUID-shaped strings do not identify real hardware.
#pragma once
#include "product/energy_collector_process.h"
#include <chrono>
#include <fstream>
#include <thread>
namespace energy_collector_fixture {
using namespace om;
constexpr int64_t T=1800000000000LL;
inline const std::vector<std::string> devices={"GPU-aaaaaaaa-0000-0000-0000-000000000001","GPU-bbbbbbbb-0000-0000-0000-000000000002"};
inline std::string bytes(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
inline void put(const std::filesystem::path &p,const std::string &s){std::ofstream f(p,std::ios::binary|std::ios::trunc);f<<s;}
inline std::filesystem::path suffix(std::filesystem::path p,const char *s){p+=s;return p;}
template<class P>bool until(P p){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(7);
    while(std::chrono::steady_clock::now()<end){if(p())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return p();}
inline EnergyAuthorizationRequest auth(const std::filesystem::path &p,EnergyAuthorizationAction a=EnergyAuthorizationAction::Load,const std::string &token={}){
    EnergyAuthorizationRequest r;r.generation=r.ticket=1;r.file=p;r.action=a;r.expected=token;r.nowUtcMs=T;
    if(a==EnergyAuthorizationAction::Enroll||a==EnergyAuthorizationAction::Provision)r.devices=devices;
    if(a==EnergyAuthorizationAction::Enroll){r.limits.compaction=EnergyCompactionPolicy{2,32768};r.policy.batchReadings=16;}
    return r;
}
inline EnergyAuthorizationReply call(EnergyAuthorizationProcessClient &client,const EnergyAuthorizationRequest &r){
    std::atomic<bool> cancel{false};const auto result=client.transact(r,cancel,5000);return result.reply?*result.reply:EnergyAuthorizationReply{};
}
inline EnergyAuthorizationReply enroll(EnergyAuthorizationProcessClient &client,const std::filesystem::path &p){
    auto d=call(client,auth(p,EnergyAuthorizationAction::Enroll,"absent"));if(d.status!=EnergyAuthorizationStatus::Ok||!d.document)return d;
    auto r=auth(p,EnergyAuthorizationAction::Save,d.token);r.policy=d.document->policy;r.policy.persistenceAuthorized=true;r.limits=d.document->limits;
    return call(client,r);
}
inline EnergyCollectorLeaseRequest lease(const std::filesystem::path &p,const EnergyAuthorizationReply &s){
    return {localTaskProcessId(),auth(p,EnergyAuthorizationAction::Provision,s.token)};
}
inline EnergyHistoryRequest history(const std::filesystem::path &p,const EnergyCollectorLeaseReply &s,bool submit=false){
    const auto &d=*s.provision.document;EnergyHistoryRequest r;r.generation=r.ticket=1;r.file=energyAuthorizationStorePath(p,d);
    r.binding=d.binding;r.limits=d.limits;r.nowUtcMs=T+10000;r.recovery=submit?EnergyRecoveryMode::Submit:EnergyRecoveryMode::Inspect;
    if(submit){EnergyHistoryBatch b;b.sequence=1;b.id="energy.1";b.sessionId=d.lastSessionId;b.observedAtMs=T+1000;
        b.start=EnergyHistorySession{b.sessionId,T,0,d.policy.maximumGapMs};b.readings={{0,T,0,0,100000},{0,T+1000,1000,0,0}};r.batch=b;}
    return r;
}
// One independent helper per holder. No check counters are touched by the background thread.
struct Held {
    std::atomic<bool> cancel{false},ready{false},done{false};
    EnergyCollectorLeaseReply reply;
    LocalTaskResult result;
    std::thread thread;
    Held(const std::filesystem::path &helper,EnergyCollectorLeaseRequest r,unsigned timeout=5000){
        thread=std::thread([this,helper,r,timeout]{std::string wire;
            if(encodeEnergyCollectorLeaseRequest(r,&wire)){LocalTaskProcess process(helper);result=process.hold(wire,cancel,timeout,[&](const std::string &frame){
                if(!decodeEnergyCollectorLeaseReply(r,frame,&reply))return false;
                ready=true;return true;});}done=true;});
    }
    bool wait(){return until([&]{return ready.load()||done.load();})&&ready;}
    void stop(){cancel=true;if(thread.joinable())thread.join();}
    ~Held(){stop();}
};
} // namespace energy_collector_fixture
