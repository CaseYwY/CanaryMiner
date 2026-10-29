#include "energy_collector_process.h"
#include "energy_history_model.h"
#include "hash.h"
#include "json_dom.h"
#include "local_store_lease.h"
#include "private_entropy.h"
#include "public_https.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#endif
#ifdef CANARY_ENERGY_COLLECTOR_TESTING
#include <cstdlib>
#endif
namespace om {
namespace {
using namespace energy_history_detail;
bool key(const std::string &s){return s.size()==32&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool array(const JsonValue &v,size_t n){return v.isArray()&&v.items.size()==n;}
bool parent(uint64_t pid){return pid&&pid<=UINT32_MAX;}
std::filesystem::path suffixed(std::filesystem::path p,const char *suffix){p+=suffix;return p;}
std::string valueText(const JsonValue &v){
    if(v.isString())return jsonQuote(v.text);
    if(v.isNumber())return v.text;
    if(v.isBool())return v.boolean?"true":"false";
    if(v.type==JsonValue::Type::Null)return "null";
    if(!v.isArray())return {};
    std::string out="[";for(size_t i=0;i<v.items.size();++i){const auto child=valueText(v.items[i]);if(child.empty())return {};if(i)out+=',';out+=child;}return out+"]";
}
bool parse(const std::string &wire,size_t max,JsonValue &v,size_t n,const char *tag){return wire.size()<=max&&boundedPublicJson(wire)&&parseJsonStrict(wire,&v,nullptr)&&array(v,n)&&v.items[0].isString()&&v.items[0].text==tag;}
bool store(const std::filesystem::path &file,const std::string &bytes){
    if(bytes.size()>energyAuthorizationLimit)return false;
    std::error_code ec;const auto st=std::filesystem::symlink_status(file,ec);
    if((ec&&ec!=std::errc::no_such_file_or_directory)||(!ec&&std::filesystem::exists(st)&&!std::filesystem::is_regular_file(st)))return false;
    static std::atomic<unsigned> serial{0};auto temp=file;
#if defined(_WIN32)
    temp+=".tmp-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++serial);
    int fd=-1;if(_wsopen_s(&fd,temp.c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY|_O_NOINHERIT,_SH_DENYRW,_S_IREAD|_S_IWRITE))return false;
    FILE *f=_fdopen(fd,"wb");
#else
    temp+=".tmp-"+std::to_string(getpid())+"-"+std::to_string(++serial);
    const int fd=::open(temp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return false;
    FILE *f=fdopen(fd,"wb");
#endif
    if(!f){
#if defined(_WIN32)
        _close(fd);
#else
        ::close(fd);
#endif
        std::filesystem::remove(temp,ec);return false;
    }
    bool ok=fwrite(bytes.data(),1,bytes.size(),f)==bytes.size()&&fflush(f)==0;
#if defined(_WIN32)
    ok=(_commit(fd)==0)&&ok;
#else
    ok=(fsync(fd)==0)&&ok;
#endif
    ok=(fclose(f)==0)&&ok;
    if(ok){
#if defined(_WIN32)
        ok=MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
        std::filesystem::rename(temp,file,ec);ok=!ec;
        if(ok){const int dir=::open(file.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);ok=dir>=0;
            if(dir>=0){ok=(fsync(dir)==0)&&ok;ok=(::close(dir)==0)&&ok;}}
#endif
    }
    if(!ok)std::filesystem::remove(temp,ec);
    return ok;
}
std::string record(const std::string &epoch,const EnergyAuthorizationDocument &doc){
    std::string encoded;if(!key(epoch)||!encodeEnergyAuthorizationDocument(doc,&encoded))return {};
    const auto body="[\"CMELF1\","+jsonQuote(epoch)+","+encoded;return body+","+jsonQuote(sha256Hex(body))+"]";
}
bool load(const std::filesystem::path &path,std::string &epoch,EnergyAuthorizationDocument &doc){
    std::error_code ec;const auto st=std::filesystem::symlink_status(path,ec);if(ec||!std::filesystem::is_regular_file(st))return false;
    std::ifstream in(path,std::ios::binary);if(!in)return false;std::string wire(energyAuthorizationLimit+1,'\0');in.read(&wire[0],static_cast<std::streamsize>(wire.size()));
    if(in.bad()||in.gcount()>static_cast<std::streamsize>(energyAuthorizationLimit))return false;
    wire.resize(static_cast<size_t>(in.gcount()));JsonValue v;
    if(!parse(wire,energyAuthorizationLimit,v,4,"CMELF1")||!v.items[1].isString()||!key(v.items[1].text)||!decodeEnergyAuthorizationDocument(valueText(v.items[2]),&doc))return false;
    epoch=v.items[1].text;return doc.enrollment==EnergyEnrollment::Ready&&doc.policy.persistenceAuthorized&&!doc.lastSessionId.empty()&&record(epoch,doc)==wire;
}
bool guardValid(const EnergyCollectorGuardRequest &r){
    return parent(r.parentPid)&&localTaskPath(r.fence.authorizationFile)&&key(r.fence.epoch)&&r.history.mode==EnergyHistoryOpen::Existing&&r.history.recovery!=EnergyRecoveryMode::Volatile;
}
}
bool encodeEnergyCollectorLeaseRequest(const EnergyCollectorLeaseRequest &r,std::string *wire){
    if(!wire||!parent(r.parentPid)||r.provision.action!=EnergyAuthorizationAction::Provision)return false;
    std::string inner;if(!encodeEnergyAuthorizationRequest(r.provision,&inner))return false;
    auto result="[\"CMELQ1\","+std::to_string(r.parentPid)+","+inner+"]";
    if(result.size()>energyAuthorizationLimit||!boundedPublicJson(result))return false;
    *wire=std::move(result);return true;
}
bool decodeEnergyCollectorLeaseRequest(const std::string &wire,EnergyCollectorLeaseRequest *out){
    if(!out)return false;
    JsonValue v;EnergyCollectorLeaseRequest r;std::string checked;
    if(!parse(wire,energyAuthorizationLimit,v,3,"CMELQ1")||!v.items[1].asUint64(&r.parentPid)||!decodeEnergyAuthorizationRequest(valueText(v.items[2]),&r.provision)||!encodeEnergyCollectorLeaseRequest(r,&checked))return false;
    *out=std::move(r);return true;
}
bool encodeEnergyCollectorLeaseReply(const EnergyCollectorLeaseRequest &r,const EnergyCollectorLeaseReply &s,std::string *wire){
    if(!wire||!key(s.epoch)||!s.provision.sessionIssued||s.provision.status!=EnergyAuthorizationStatus::Ok)return false;
    std::string inner,request;if(!encodeEnergyCollectorLeaseRequest(r,&request)||!encodeEnergyAuthorizationReply(r.provision,s.provision,&inner))return false;
    auto result="[\"CMELA1\","+jsonQuote(s.epoch)+","+inner+","+jsonQuote(sha256Hex(request))+"]";
    if(result.size()>=energyAuthorizationLimit||!boundedPublicJson(result))return false;
    *wire=std::move(result);return true;
}
bool decodeEnergyCollectorLeaseReply(const EnergyCollectorLeaseRequest &r,const std::string &wire,EnergyCollectorLeaseReply *out){
    if(!out)return false;
    JsonValue v;EnergyCollectorLeaseReply s;std::string request;
    if(!encodeEnergyCollectorLeaseRequest(r,&request)||!parse(wire,energyAuthorizationLimit,v,4,"CMELA1")||!v.items[1].isString()||!key(v.items[1].text)||
        !v.items[3].isString()||v.items[3].text!=sha256Hex(request)||!decodeEnergyAuthorizationReply(r.provision,valueText(v.items[2]),&s.provision)||!s.provision.sessionIssued)return false;
    s.epoch=v.items[1].text;*out=std::move(s);return true;
}
bool encodeEnergyCollectorGuardRequest(const EnergyCollectorGuardRequest &r,std::string *wire){
    if(!wire||!guardValid(r))return false;
    std::string inner;if(!encodeEnergyHistoryRequest(r.history,&inner))return false;
    auto result="[\"CMELW1\","+std::to_string(r.parentPid)+","+jsonQuote(r.fence.authorizationFile.u8string())+","+jsonQuote(r.fence.epoch)+","+inner+"]";
    if(result.size()>energyHistoryRequestLimit||!boundedPublicJson(result))return false;
    *wire=std::move(result);return true;
}
bool decodeEnergyCollectorGuardRequest(const std::string &wire,EnergyCollectorGuardRequest *out){
    if(!out)return false;
    JsonValue v;EnergyCollectorGuardRequest r;std::string checked;
    if(!parse(wire,energyHistoryRequestLimit,v,5,"CMELW1")||!v.items[1].asUint64(&r.parentPid)||!v.items[2].isString()||v.items[2].text.size()>4096||v.items[2].text.find('\0')!=std::string::npos||
        !v.items[3].isString()||!decodeEnergyHistoryRequest(valueText(v.items[4]),&r.history))return false;
    try{r.fence.authorizationFile=std::filesystem::u8path(v.items[2].text);}catch(...){return false;}
    r.fence.epoch=v.items[3].text;if(!encodeEnergyCollectorGuardRequest(r,&checked))return false;*out=std::move(r);return true;
}
int serveEnergyCollectorLease(const EnergyCollectorLeaseRequest &r){
    std::string checked;if(!encodeEnergyCollectorLeaseRequest(r,&checked)||!bindLocalTaskParent(r.parentPid))return 2;
    const auto &path=r.provision.file;LocalStoreLease lifetime;
    if(!lifetime.acquire(suffixed(path,".collector.lock")))return 3; // no waiting or stealing a live collector
    EnergyCollectorLeaseReply reply;
    {
        LocalStoreLease operation;if(!operation.acquire(suffixed(path,".collector-operation.lock")))return 4;
        const auto recordPath=suffixed(path,".collector.json");std::error_code ec;const auto prior=std::filesystem::symlink_status(recordPath,ec);
        const bool absent=ec==std::errc::no_such_file_or_directory||(!ec&&prior.type()==std::filesystem::file_type::not_found);
        EnergyAuthorizationDocument previous;std::string previousEpoch;
        if(!absent&&(ec||!load(recordPath,previousEpoch,previous)))return 5;
        if(!absent){auto inspection=r.provision;inspection.action=EnergyAuthorizationAction::Load;inspection.expected.clear();inspection.devices.clear();
            const auto current=executeEnergyAuthorizationRequest(inspection);
            if(current.status!=EnergyAuthorizationStatus::Ok||!current.document||current.token!=r.provision.expected||
                current.document->revision<previous.revision||current.document->changedUtcMs<previous.changedUtcMs||
                bindingText(previous.binding)!=bindingText(current.document->binding)||!sameEnergyHistoryLimits(previous.limits,current.document->limits))return 6;}
        std::string epoch;if(!privateEnergyKey(&epoch)||epoch==previousEpoch)return 5;
        reply.provision=executeEnergyAuthorizationRequest(r.provision);
        if(reply.provision.status!=EnergyAuthorizationStatus::Ok||!reply.provision.sessionIssued)return 6;
        if(!absent&&(bindingText(previous.binding)!=bindingText(reply.provision.document->binding)||!sameEnergyHistoryLimits(previous.limits,reply.provision.document->limits)))return 6;
        reply.epoch=epoch;const auto saved=record(epoch,*reply.provision.document);
        if(saved.empty()||!store(recordPath,saved))return 7;
    }
#ifdef CANARY_ENERGY_COLLECTOR_TESTING
    if(path.filename()=="fixture-parent-death.json")std::ofstream(suffixed(path,".fixture-pid"))<<localTaskProcessId();
    if(path.filename()=="fixture-lease-no-ack.json")std::this_thread::sleep_for(std::chrono::seconds(60));
#endif
    if(!encodeEnergyCollectorLeaseReply(r,reply,&checked)||!writeLocalTaskOutput(checked+"\n"))return 8;
    for(;;){
#ifdef CANARY_ENERGY_COLLECTOR_TESTING
        if(path.filename()=="fixture-lease-exit.json"&&std::filesystem::exists(suffixed(path,".fixture-exit")))std::_Exit(84);
        if(path.filename()=="fixture-lease-extra.json"){writeLocalTaskOutput("unexpected");std::this_thread::sleep_for(std::chrono::seconds(60));}
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
EnergyHistoryReply executeEnergyCollectorGuard(const EnergyCollectorGuardRequest &r){
    EnergyHistoryReply reply;reply.generation=r.history.generation;reply.ticket=r.history.ticket;reply.opened=EnergyHistoryStatus::Attribution;
    reply.recovery=EnergyRecoverySnapshot{};reply.recovery->status=reply.opened;
    std::string checked;if(!encodeEnergyCollectorGuardRequest(r,&checked)||!bindLocalTaskParent(r.parentPid))return reply;
#ifdef CANARY_ENERGY_COLLECTOR_TESTING
    if((r.fence.authorizationFile.filename()=="fixture-guard-delay.json"||r.fence.authorizationFile.filename()=="fixture-parent-death.json")&&r.history.batch){
        std::ofstream(suffixed(r.fence.authorizationFile,".fixture-guard-pid"))<<localTaskProcessId();
        std::ofstream(suffixed(r.fence.authorizationFile,".fixture-ready")).put('1');
        for(unsigned n=0;n<3000&&!std::filesystem::exists(suffixed(r.fence.authorizationFile,".fixture-release"));++n)std::this_thread::sleep_for(std::chrono::milliseconds(20));}
#endif
    LocalStoreLease operation;
    if(!operation.acquire(suffixed(r.fence.authorizationFile,".collector-operation.lock"))){reply.opened=EnergyHistoryStatus::Storage;reply.recovery->status=reply.opened;return reply;}
    const auto held=localStoreLeaseHeld(suffixed(r.fence.authorizationFile,".collector.lock"));
    if(!held||!*held)return reply;
    EnergyAuthorizationDocument d;std::string epoch;
    if(!load(suffixed(r.fence.authorizationFile,".collector.json"),epoch,d)||epoch!=r.fence.epoch||r.history.file!=energyAuthorizationStorePath(r.fence.authorizationFile,d)||
        bindingText(r.history.binding)!=bindingText(d.binding)||!sameEnergyHistoryLimits(r.history.limits,d.limits))return reply;
    if(r.history.nowUtcMs<d.changedUtcMs){reply.opened=EnergyHistoryStatus::Clock;reply.recovery->status=reply.opened;return reply;}
    if(r.history.recovery==EnergyRecoveryMode::Submit&&(!r.history.batch||r.history.batch->sessionId!=d.lastSessionId||r.history.batch->readings.size()>d.policy.batchReadings||
        r.history.batch->observedAtMs<d.changedUtcMs||
        (r.history.batch->start&&(r.history.batch->start->maximumGapMs!=d.policy.maximumGapMs||r.history.batch->start->anchorUtcMs<d.changedUtcMs))))return reply;
#ifdef CANARY_ENERGY_COLLECTOR_TESTING
    if(r.fence.authorizationFile.filename()=="fixture-guard-in-operation.json"&&r.history.batch){
        std::ofstream(suffixed(r.fence.authorizationFile,".fixture-ready")).put('1');
        for(unsigned n=0;n<3000&&!std::filesystem::exists(suffixed(r.fence.authorizationFile,".fixture-release"));++n)std::this_thread::sleep_for(std::chrono::milliseconds(20));}
#endif
    // Held until the journal/database transaction and receipt complete. A successor cannot publish its fence mid-write.
    return executeEnergyHistoryRequest(r.history);
}
} // namespace om
