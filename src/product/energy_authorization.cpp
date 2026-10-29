#include "energy_authorization.h"
#include "energy_history_model.h"
#include "hash.h"
#include "json_dom.h"
#include "local_store_lease.h"
#include "public_https.h"
#include "private_entropy.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#endif
#ifdef CANARY_ENERGY_AUTHORIZATION_TESTING
#include <cstdlib>
#endif

namespace om {
namespace {
using namespace energy_history_detail;
using A=EnergyAuthorizationAction;
using S=EnergyAuthorizationStatus;
using E=EnergyEnrollment;
bool hex(const std::string &s,size_t n){return s.size()==n&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool array(const JsonValue &v,size_t n){return v.isArray()&&v.items.size()==n;}
bool number(const JsonValue &v,uint64_t &n,uint64_t max=INT64_MAX){return v.asUint64(&n)&&n<=max;}
bool text(const JsonValue &v,std::string &s,size_t max){if(!v.isString()||v.text.size()>max||v.text.find('\0')!=std::string::npos)return false;s=v.text;return true;}
bool boolean(const JsonValue &v,bool &b){if(!v.isBool())return false;b=v.boolean;return true;}
bool policyValid(const EnergyCapturePolicy &p){return p.selectionMs>=500&&p.selectionMs<=60000&&p.maximumGapMs>=p.selectionMs&&p.maximumGapMs<=60000&&
    p.flushMs>=50&&p.flushMs<=60000&&p.bufferedReadings>=1&&p.bufferedReadings<=128&&p.batchReadings>=1&&p.batchReadings<=p.bufferedReadings;}
std::string policyText(const EnergyCapturePolicy &p){return std::string("[")+(p.persistenceAuthorized?"true":"false")+","+std::to_string(p.selectionMs)+","+
    std::to_string(p.maximumGapMs)+","+std::to_string(p.flushMs)+","+std::to_string(p.bufferedReadings)+","+std::to_string(p.batchReadings)+"]";}
bool policyValue(const JsonValue &v,EnergyCapturePolicy &p){uint64_t n[5]={};if(!array(v,6)||!boolean(v.items[0],p.persistenceAuthorized))return false;
    for(size_t i=0;i<5;++i)if(!number(v.items[i+1],n[i],60000))return false;
    p.selectionMs=static_cast<unsigned>(n[0]);p.maximumGapMs=static_cast<unsigned>(n[1]);p.flushMs=static_cast<unsigned>(n[2]);
    p.bufferedReadings=static_cast<size_t>(n[3]);p.batchReadings=static_cast<size_t>(n[4]);return policyValid(p);}
std::string limitsText(const EnergyHistoryLimits &l){return "["+std::to_string(l.maxBatches)+","+std::to_string(l.maxReadings)+","+std::to_string(l.maxSessions)+","+
    std::to_string(l.maxWindows)+","+std::to_string(l.maxDatabaseBytes)+","+(l.compaction?"["+std::to_string(l.compaction->afterBatches)+","+std::to_string(l.compaction->maxSegments)+"]":"null")+"]";}
bool limitsValue(const JsonValue &v,EnergyHistoryLimits &l){uint64_t n[5]={};if(!array(v,6))return false;
    for(size_t i=0;i<5;++i)if(!number(v.items[i],n[i],128*1024*1024))return false;
    l={static_cast<size_t>(n[0]),static_cast<size_t>(n[1]),static_cast<size_t>(n[2]),static_cast<size_t>(n[3]),n[4]};
    if(v.items[5].type!=JsonValue::Type::Null){uint64_t after=0,segments=0;if(!array(v.items[5],2)||!number(v.items[5].items[0],after,4096)||!number(v.items[5].items[1],segments,32768))return false;
        l.compaction=EnergyCompactionPolicy{static_cast<size_t>(after),static_cast<size_t>(segments)};}
    return validEnergyHistoryLimits(l);}
bool emptyBinding(const EnergyHistoryBinding &b){return b.channels.empty()&&!b.historyBeginUtcMs&&b.evidence.basis==PowerBasis::Unknown&&
    b.evidence.origin==PowerOrigin::Unknown&&b.evidence.scope.empty()&&b.evidence.adapter.empty();}
std::string documentBody(const EnergyAuthorizationDocument &d){return "[\"CMEP1\","+std::to_string(d.revision)+","+std::to_string(d.changedUtcMs)+","+policyText(d.policy)+","+
    limitsText(d.limits)+","+std::to_string(static_cast<unsigned>(d.enrollment))+","+jsonQuote(d.privateNamespace)+","+
    (d.enrollment==E::None?"null":bindingText(d.binding))+","+jsonQuote(d.lastSessionId);}
bool documentValue(const JsonValue &v,EnergyAuthorizationDocument &d){uint64_t when=0,state=0;
    if(!array(v,10)||!v.items[0].isString()||v.items[0].text!="CMEP1"||!number(v.items[1],d.revision,lifetimeBatchLimit)||!number(v.items[2],when,lastUtcMs)||
        !policyValue(v.items[3],d.policy)||!limitsValue(v.items[4],d.limits)||!number(v.items[5],state,2)||!text(v.items[6],d.privateNamespace,32)||!text(v.items[8],d.lastSessionId,40))return false;
    d.changedUtcMs=static_cast<int64_t>(when);d.enrollment=static_cast<E>(state);
    if(d.enrollment==E::None){if(v.items[7].type!=JsonValue::Type::Null)return false;}
    else if(!parseBindingValue(v.items[7],&d.binding))return false;
    return validEnergyAuthorizationDocument(d)&&v.items[9].isString()&&v.items[9].text==sha256Hex(documentBody(d));}
bool selected(const EnergyAuthorizationDocument &d,const std::vector<std::string> &ids){EnergyHistoryBinding b;
    return makeEnergyCaptureBinding(d.privateNamespace,d.binding.historyBeginUtcMs,ids,PowerOrigin::ObservedTelemetry,&b)&&bindingText(b)==bindingText(d.binding);}
bool valid(const EnergyAuthorizationRequest &r){
    if(!r.generation||!r.ticket||!localTaskPath(r.file)||!instant(r.nowUtcMs)||!policyValid(r.policy)||!validEnergyHistoryLimits(r.limits)||r.policy.batchReadings>r.limits.maxReadings)return false;
    if(r.action==A::Load){if(!r.expected.empty())return false;}else if(r.expected!="absent"&&!hex(r.expected,64))return false;
    switch(r.action){
        case A::Enroll:if(r.policy.persistenceAuthorized)return false;[[fallthrough]];
        case A::Provision:{EnergyHistoryBinding b;if(!makeEnergyCaptureBinding(std::string(32,'0'),r.nowUtcMs,r.devices,PowerOrigin::ObservedTelemetry,&b))return false;break;}
        case A::Load:case A::Save:case A::Inspect:case A::Confirm:if(!r.devices.empty())return false;break;
        default:return false;
    }
    // Reject ignored knobs: every received field has one interpretation.
    if(r.action!=A::Save&&r.action!=A::Enroll&&(policyText(r.policy)!=policyText(EnergyCapturePolicy{})||!sameEnergyHistoryLimits(r.limits,EnergyHistoryLimits{})))return false;
    return true;
}
bool mutating(A a){return a==A::Save||a==A::Enroll||a==A::Confirm||a==A::Provision;}
bool randomKey(std::string &out){
    return privateEnergyKey(&out);
}
S readDocument(const std::filesystem::path &file,EnergyAuthorizationReply &reply){
    std::error_code ec;const auto st=std::filesystem::symlink_status(file,ec);
    if(ec==std::errc::no_such_file_or_directory||(!ec&&st.type()==std::filesystem::file_type::not_found)){reply.document=EnergyAuthorizationDocument{};reply.token="absent";return S::Missing;}
    if(ec||!std::filesystem::is_regular_file(st))return S::Storage;
    std::ifstream in(file,std::ios::binary);if(!in)return S::Storage;
    std::string bytes(energyAuthorizationLimit+1,'\0');in.read(&bytes[0],static_cast<std::streamsize>(bytes.size()));
    if(in.bad()||in.gcount()>static_cast<std::streamsize>(energyAuthorizationLimit))return S::Storage;
    bytes.resize(static_cast<size_t>(in.gcount()));EnergyAuthorizationDocument d;
    if(!decodeEnergyAuthorizationDocument(bytes,&d))return S::Storage;
    reply.document=std::move(d);reply.token=sha256Hex(bytes);return S::Ok;
}
bool writeDocument(const std::filesystem::path &file,const EnergyAuthorizationDocument &d,EnergyAuthorizationReply &reply){
    std::string bytes;if(!encodeEnergyAuthorizationDocument(d,&bytes))return false;
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
    if(!ok){std::filesystem::remove(temp,ec);return false;}
    reply.document=d;reply.token=sha256Hex(bytes);return true;
}
EnergyHistoryReply inspectStore(const EnergyAuthorizationRequest &r,const EnergyAuthorizationDocument &d,EnergyHistoryOpen mode){
    EnergyHistoryRequest q;q.generation=r.generation;q.ticket=r.ticket;q.file=energyAuthorizationStorePath(r.file,d);q.binding=d.binding;q.limits=d.limits;
    q.mode=mode;q.nowUtcMs=r.nowUtcMs;q.recovery=EnergyRecoveryMode::Inspect;return executeEnergyHistoryRequest(q);
}
bool goodStore(const EnergyHistoryReply &r){return r.opened==EnergyHistoryStatus::Ok&&r.checkpoint.status==EnergyHistoryStatus::Ok&&r.recovery&&
    r.recovery->status==EnergyHistoryStatus::Ok&&r.recovery->state!=EnergyRecoveryState::Diverged&&r.recovery->state!=EnergyRecoveryState::Unavailable&&r.recovery->state!=EnergyRecoveryState::Disabled;}
bool emptyStore(const EnergyHistoryReply &r,const EnergyAuthorizationDocument &d){return goodStore(r)&&!r.checkpoint.sequence&&r.checkpoint.digest==sha256Hex(bindingText(d.binding))&&
    r.recovery->state==EnergyRecoveryState::Empty&&!r.recovery->batch;}
#ifdef CANARY_ENERGY_AUTHORIZATION_TESTING
void crashAt(const EnergyAuthorizationRequest &r,const char *point,int exit){if(r.file.filename()==std::string("fixture-")+point+".json")std::_Exit(exit);}
#endif
} // namespace

bool validEnergyAuthorizationDocument(const EnergyAuthorizationDocument &d){
    if(d.revision>lifetimeBatchLimit||d.lastSessionId.size()>40||!policyValid(d.policy)||!validEnergyHistoryLimits(d.limits)||d.policy.batchReadings>d.limits.maxReadings)return false;
    if(!d.revision)return !d.changedUtcMs&&d.enrollment==E::None&&!d.policy.persistenceAuthorized&&d.privateNamespace.empty()&&d.lastSessionId.empty()&&emptyBinding(d.binding)&&
        policyText(d.policy)==policyText(EnergyCapturePolicy{})&&sameEnergyHistoryLimits(d.limits,EnergyHistoryLimits{});
    if(!instant(d.changedUtcMs))return false;
    if(d.enrollment==E::None)return !d.policy.persistenceAuthorized&&d.privateNamespace.empty()&&d.lastSessionId.empty()&&emptyBinding(d.binding);
    if((d.enrollment!=E::Pending&&d.enrollment!=E::Ready)||!hex(d.privateNamespace,32)||!validEnergyHistoryBinding(d.binding)||
        d.binding.evidence.origin!=PowerOrigin::ObservedTelemetry||d.binding.historyBeginUtcMs>d.changedUtcMs||!std::is_sorted(d.binding.channels.begin(),d.binding.channels.end()))return false;
    for(const auto &key:d.binding.channels)if(key.rfind("channel.",0)!=0||!hex(key.substr(8),64))return false;
    // Recompute the selected-set provenance without retaining raw device UUIDs.
    std::string selection="energy-selection-v1|"+d.privateNamespace;for(const auto &key:d.binding.channels)selection+="|"+key;
    if(d.binding.evidence.basis!=PowerBasis::GpuBoard||d.binding.evidence.adapter!="native-nvml-milliwatts"||d.binding.evidence.scope!="selection."+sha256Hex(selection))return false;
    if(d.enrollment==E::Pending)return !d.policy.persistenceAuthorized&&d.lastSessionId.empty();
    return d.lastSessionId.empty()||(d.lastSessionId.rfind("session.",0)==0&&hex(d.lastSessionId.substr(8),32));
}
bool encodeEnergyAuthorizationDocument(const EnergyAuthorizationDocument &d,std::string *out){
    if(!out||!d.revision||!validEnergyAuthorizationDocument(d))return false;
    const auto body=documentBody(d);auto result=body+","+jsonQuote(sha256Hex(body))+"]";
    if(result.size()>energyAuthorizationLimit||!boundedPublicJson(result))return false;
    *out=std::move(result);return true;
}
bool decodeEnergyAuthorizationDocument(const std::string &wire,EnergyAuthorizationDocument *out){
    if(!out||wire.size()>energyAuthorizationLimit||!boundedPublicJson(wire))return false;
    JsonValue v;EnergyAuthorizationDocument d;std::string canonical;
    if(!parseJsonStrict(wire,&v,nullptr)||!documentValue(v,d)||!encodeEnergyAuthorizationDocument(d,&canonical)||wire!=canonical)return false;
    *out=std::move(d);return true;
}
std::filesystem::path energyAuthorizationStorePath(const std::filesystem::path &file,const EnergyAuthorizationDocument &d){
    if(!localTaskPath(file)||!validEnergyAuthorizationDocument(d)||d.enrollment==E::None)return {};
    return file.parent_path()/("energy-history."+d.privateNamespace+".db");
}
bool encodeEnergyAuthorizationRequest(const EnergyAuthorizationRequest &r,std::string *out){
    if(!out||!valid(r))return false;
    std::string wire="[\"CMEPQ1\","+std::to_string(r.generation)+","+std::to_string(r.ticket)+","+jsonQuote(r.file.u8string())+","+std::to_string(static_cast<unsigned>(r.action))+","+
        jsonQuote(r.expected)+","+std::to_string(r.nowUtcMs)+","+policyText(r.policy)+","+limitsText(r.limits)+",[";
    for(size_t i=0;i<r.devices.size();++i){if(i)wire+=',';wire+=jsonQuote(r.devices[i]);}wire+="]]";
    if(wire.size()>energyAuthorizationLimit||!boundedPublicJson(wire))return false;
    *out=std::move(wire);return true;
}
bool decodeEnergyAuthorizationRequest(const std::string &wire,EnergyAuthorizationRequest *out){
    if(!out||wire.size()>energyAuthorizationLimit||!boundedPublicJson(wire))return false;
    JsonValue v;EnergyAuthorizationRequest r;uint64_t action=0,when=0;std::string path;
    if(!parseJsonStrict(wire,&v,nullptr)||!array(v,10)||!v.items[0].isString()||v.items[0].text!="CMEPQ1"||!number(v.items[1],r.generation,UINT64_MAX)||!number(v.items[2],r.ticket,UINT64_MAX)||
        !text(v.items[3],path,4096)||!number(v.items[4],action,5)||!text(v.items[5],r.expected,64)||!number(v.items[6],when,lastUtcMs)||!policyValue(v.items[7],r.policy)||!limitsValue(v.items[8],r.limits)||
        !v.items[9].isArray()||v.items[9].items.size()>64)return false;
    for(const auto &item:v.items[9].items){std::string id;if(!text(item,id,40))return false;r.devices.push_back(std::move(id));}
    try{r.file=std::filesystem::u8path(path);}catch(...){return false;}
    r.action=static_cast<A>(action);r.nowUtcMs=static_cast<int64_t>(when);if(!valid(r))return false;
    *out=std::move(r);return true;
}
bool encodeEnergyAuthorizationReply(const EnergyAuthorizationRequest &r,const EnergyAuthorizationReply &s,std::string *out){
    if(!out||!valid(r))return false;
    std::string request;if(!encodeEnergyAuthorizationRequest(r,&request))return false;
    std::string doc="null";if(s.document&&s.document->revision&&!encodeEnergyAuthorizationDocument(*s.document,&doc))return false;
    std::string wire="[\"CMEPA1\","+std::to_string(s.generation)+","+std::to_string(s.ticket)+","+std::to_string(static_cast<unsigned>(s.status))+","+doc+","+jsonQuote(s.token)+","+
        (s.emptyStoreVerified?"true":"false")+","+(s.sessionIssued?"true":"false")+","+jsonQuote(sha256Hex(request))+"]";
    EnergyAuthorizationReply checked;if(!decodeEnergyAuthorizationReply(r,wire,&checked))return false;
    *out=std::move(wire);return true;
}
bool decodeEnergyAuthorizationReply(const EnergyAuthorizationRequest &r,const std::string &wire,EnergyAuthorizationReply *out){
    if(!out||!valid(r)||wire.size()>energyAuthorizationLimit||!boundedPublicJson(wire))return false;
    JsonValue v;EnergyAuthorizationReply s;uint64_t status=0;std::string request;
    if(!encodeEnergyAuthorizationRequest(r,&request)||!parseJsonStrict(wire,&v,nullptr)||!array(v,9)||!v.items[8].isString()||v.items[8].text!=sha256Hex(request)||
        !v.items[0].isString()||v.items[0].text!="CMEPA1"||!number(v.items[1],s.generation,UINT64_MAX)||!number(v.items[2],s.ticket,UINT64_MAX)||
        s.generation!=r.generation||s.ticket!=r.ticket||!number(v.items[3],status,9)||!text(v.items[5],s.token,64)||!boolean(v.items[6],s.emptyStoreVerified)||!boolean(v.items[7],s.sessionIssued))return false;
    s.status=static_cast<S>(status);
    if(v.items[4].type!=JsonValue::Type::Null){EnergyAuthorizationDocument d;std::string encoded;
        if(!documentValue(v.items[4],d)||!encodeEnergyAuthorizationDocument(d,&encoded)||s.token!=sha256Hex(encoded))return false;
        s.document=std::move(d);}
    else if(s.token=="absent")s.document=EnergyAuthorizationDocument{};
    else if(!s.token.empty())return false;
    if(s.status==S::Ok&&(!s.document||!s.document->revision))return false;
    if(s.status==S::Missing&&(r.action!=A::Load||s.token!="absent"))return false;
    if(s.emptyStoreVerified&&(s.status!=S::Ok||!s.document||s.document->enrollment==E::None||(r.action!=A::Enroll&&r.action!=A::Inspect&&r.action!=A::Confirm)))return false;
    if(s.sessionIssued!=(r.action==A::Provision&&s.status==S::Ok))return false;
    if(s.sessionIssued&&(!s.document||s.document->enrollment!=E::Ready||!s.document->policy.persistenceAuthorized||s.document->lastSessionId.empty()||!selected(*s.document,r.devices)))return false;
    if(s.status==S::Ok){
        if(s.document->changedUtcMs>r.nowUtcMs)return false;
        if(r.action==A::Enroll&&(s.document->enrollment!=E::Ready||!selected(*s.document,r.devices)||!s.emptyStoreVerified||
            s.document->binding.historyBeginUtcMs!=r.nowUtcMs||!s.document->lastSessionId.empty()||policyText(s.document->policy)!=policyText(r.policy)||!sameEnergyHistoryLimits(s.document->limits,r.limits)))return false;
        if(r.action==A::Confirm&&(s.document->enrollment!=E::Ready||!s.emptyStoreVerified))return false;
        if(r.action==A::Inspect&&!s.emptyStoreVerified)return false;
        if(r.action==A::Save&&(policyText(s.document->policy)!=policyText(r.policy)||!sameEnergyHistoryLimits(s.document->limits,r.limits)))return false;
    }
    *out=std::move(s);return true;
}
EnergyAuthorizationReply executeEnergyAuthorizationRequest(const EnergyAuthorizationRequest &r){
    EnergyAuthorizationReply reply;reply.generation=r.generation;reply.ticket=r.ticket;
    if(!valid(r)){reply.status=S::Invalid;return reply;}
    if(r.action==A::Load){reply.status=readDocument(r.file,reply);if(reply.document&&reply.document->changedUtcMs>r.nowUtcMs)reply.status=S::Clock;return reply;}
    std::error_code ec;
    if(mutating(r.action))std::filesystem::create_directories(r.file.parent_path(),ec);
    if(ec)return reply;
    LocalStoreLease lease;auto lock=r.file;lock+=".lock";if(!lease.acquire(lock))return reply;
    const auto loaded=readDocument(r.file,reply);if(loaded!=S::Ok&&loaded!=S::Missing)return reply;
    auto d=*reply.document;
    if(reply.token!=r.expected){reply.status=S::Conflict;return reply;}
    if(d.changedUtcMs>r.nowUtcMs){reply.status=S::Clock;return reply;}
    if(d.revision>=lifetimeBatchLimit-1){reply.status=S::Conflict;return reply;}
    auto save=[&](){++d.revision;d.changedUtcMs=r.nowUtcMs;const bool ok=writeDocument(r.file,d,reply);reply.status=ok?S::Ok:S::Storage;return ok;};
    if(r.action==A::Save){
        if((d.enrollment!=E::Ready&&r.policy.persistenceAuthorized)||(d.enrollment!=E::None&&!sameEnergyHistoryLimits(d.limits,r.limits))){reply.status=S::Conflict;return reply;}
        d.policy=r.policy;d.limits=r.limits;save();return reply;
    }
    if(r.action==A::Enroll){
        if(d.enrollment!=E::None){reply.status=S::Conflict;return reply;}
#ifdef CANARY_ENERGY_AUTHORIZATION_TESTING
        if(r.file.filename()=="fixture-entropy-fail.json"){reply.status=S::Entropy;return reply;}
#endif
        if(!randomKey(d.privateNamespace)){reply.status=S::Entropy;return reply;}
        if(!makeEnergyCaptureBinding(d.privateNamespace,r.nowUtcMs,r.devices,PowerOrigin::ObservedTelemetry,&d.binding)){reply.status=S::Selection;return reply;}
        d.policy=r.policy;d.limits=r.limits;d.enrollment=E::Pending;
        // A durable identity reservation MUST precede every database side effect. Never retry creation from Pending.
        if(!save())return reply;
#ifdef CANARY_ENERGY_AUTHORIZATION_TESTING
        crashAt(r,"exit-reserved",81);
#endif
        if(!emptyStore(inspectStore(r,d,EnergyHistoryOpen::CreateNew),d)){reply.status=S::NeedsReview;return reply;}
#ifdef CANARY_ENERGY_AUTHORIZATION_TESTING
        crashAt(r,"exit-created",82);
#endif
        d.enrollment=E::Ready;if(save())reply.emptyStoreVerified=true;return reply;
    }
    if(r.action==A::Inspect||r.action==A::Confirm){
        if(d.enrollment!=E::Pending){reply.status=S::Conflict;return reply;}
        if(!emptyStore(inspectStore(r,d,EnergyHistoryOpen::Existing),d)){reply.status=S::NeedsReview;return reply;}
        if(r.action==A::Inspect){reply.emptyStoreVerified=true;reply.status=S::Ok;return reply;}
        d.enrollment=E::Ready;if(save())reply.emptyStoreVerified=true;return reply;
    }
    if(d.enrollment!=E::Ready||!d.policy.persistenceAuthorized){reply.status=S::Disabled;return reply;}
    if(!selected(d,r.devices)){reply.status=S::Selection;return reply;}
    if(!goodStore(inspectStore(r,d,EnergyHistoryOpen::Existing))){reply.status=S::NeedsReview;return reply;}
    std::string key;if(!randomKey(key)||"session."+key==d.lastSessionId){reply.status=S::Entropy;return reply;}
    d.lastSessionId="session."+key;
    if(save()){
#ifdef CANARY_ENERGY_AUTHORIZATION_TESTING
        crashAt(r,"exit-issued",83);
#endif
        reply.sessionIssued=true;
    }
    return reply;
}
EnergyAuthorizationProcessResult EnergyAuthorizationProcessClient::transact(const EnergyAuthorizationRequest &r,const std::atomic<bool> &cancel,unsigned timeout){
    EnergyAuthorizationProcessResult result;std::string wire;
    if(cancel){result.transport=LocalTaskStatus::Cancelled;return result;}
    if(!encodeEnergyAuthorizationRequest(r,&wire)){result.transport=LocalTaskStatus::Invalid;return result;}
    const auto task=process_.run(wire,cancel,timeout);result.transport=task.status;
    if(task.status==LocalTaskStatus::Ok){EnergyAuthorizationReply reply;if(decodeEnergyAuthorizationReply(r,task.response,&reply))result.reply=std::move(reply);else result.transport=LocalTaskStatus::Unavailable;}
    result.writeOutcomeUnknown=mutating(r.action)&&task.childStarted&&(!result.reply||result.reply->status==S::Storage);
    return result;
}
} // namespace om
