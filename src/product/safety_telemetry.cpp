#include "safety_telemetry.h"
#include "hash.h"
#include <charconv>
#include <chrono>
#include <set>
namespace om {
namespace {
bool uuid(const std::string &s){
    if(s.size()!=40||s.compare(0,4,"GPU-"))return false;
    for(size_t i=4;i<s.size();++i){if(i==12||i==17||i==22||i==27){if(s[i]!='-')return false;}
        else if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;}
    return s!="GPU-00000000-0000-0000-0000-000000000000";
}
bool time(GameGuardTime t){return t.utcMs>0&&t.utcMs<=253402300799999LL;}
bool ordered(GameGuardTime a,GameGuardTime b){return a.utcMs<=b.utcMs&&a.monotonicMs<=b.monotonicMs;}
struct Reader {
    const std::string &s;size_t at=0;
    bool line(std::string &v,size_t maximum=64){const auto end=s.find('\n',at);if(end==s.npos||end-at>maximum)return false;v=s.substr(at,end-at);at=end+1;return true;}
    bool number(uint64_t &v,uint64_t maximum=UINT64_MAX){std::string text;if(!line(text,20)||text.empty()||(text.size()>1&&text[0]=='0')||text[0]<'0'||text[0]>'9')return false;
        const auto p=std::from_chars(text.data(),text.data()+text.size(),v);return p.ec==std::errc{}&&p.ptr==text.data()+text.size()&&v<=maximum;}
    bool instant(GameGuardTime &v){uint64_t utc=0;if(!number(utc,253402300799999ULL)||!utc||!number(v.monotonicMs))return false;v.utcMs=static_cast<int64_t>(utc);return true;}
};
void instant(std::string &s,GameGuardTime t){s+=std::to_string(t.utcMs)+"\n"+std::to_string(t.monotonicMs)+"\n";}
std::string binding(const SafetyTelemetryRequest &r){std::string s;return encodeSafetyTelemetryRequest(r,1,&s)?sha256Hex(s):std::string();}
bool validReply(const SafetyTelemetryRequest &r,const SafetyTelemetryReply &v){
    if(!validSafetyTelemetryRequest(r)||static_cast<unsigned>(v.status)>static_cast<unsigned>(SafetyTelemetryStatus::Invalid)||!time(v.began)||!time(v.finished))return false;
    // Failed batches preserve actual clocks, including rollback. They can never contain usable readings.
    if(v.status!=SafetyTelemetryStatus::Ok)return v.readings.empty()&&(r.enabled?v.status!=SafetyTelemetryStatus::Disabled:v.status==SafetyTelemetryStatus::Disabled);
    if(!r.enabled||!ordered(r.issued,v.began)||!ordered(v.began,v.finished)||v.readings.size()!=r.devices.size()||static_cast<uint64_t>(v.finished.utcMs-v.began.utcMs)>r.collectionBudgetMs||
        v.finished.monotonicMs-v.began.monotonicMs>r.collectionBudgetMs)return false;
    const auto wall=static_cast<uint64_t>(v.finished.utcMs-v.began.utcMs),mono=v.finished.monotonicMs-v.began.monotonicMs;
    if((wall>mono?wall-mono:mono-wall)>100)return false;
    for(const auto &reading:v.readings)if(reading.temperatureC< -273||reading.temperatureC>250||
        (reading.activeClockEvents&~reading.supportedClockEvents))return false;
    return true;
}
}
GameGuardTime safetyTelemetryTime(){return {std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(),
    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())};}
bool validSafetyTelemetryRequest(const SafetyTelemetryRequest &r){
    if(!r.epoch||!r.revision||!time(r.issued)||r.collectionBudgetMs<25||r.collectionBudgetMs>1000||r.devices.empty()||r.devices.size()>safetyTelemetryDeviceLimit)return false;
    std::set<std::string> seen;for(const auto &d:r.devices)if(!uuid(d)||!seen.insert(d).second)return false;return true;
}
bool encodeSafetyTelemetryRequest(const SafetyTelemetryRequest &r,uint64_t parent,std::string *wire){
    if(!wire||!parent||parent>UINT32_MAX||!validSafetyTelemetryRequest(r))return false;
    auto s=std::string("CMSTQ1\n")+std::to_string(parent)+"\n"+(r.enabled?"1\n":"0\n")+std::to_string(r.epoch)+"\n"+std::to_string(r.revision)+"\n";
    instant(s,r.issued);s+=std::to_string(r.collectionBudgetMs)+"\n"+std::to_string(r.devices.size())+"\n";
    for(const auto &d:r.devices)s+=d+"\n";
    if(s.size()>safetyTelemetryWireLimit)return false;
    *wire=std::move(s);return true;
}
bool decodeSafetyTelemetryRequest(const std::string &wire,SafetyTelemetryRequest *out,uint64_t *parent){
    if(!out||!parent||wire.size()>safetyTelemetryWireLimit)return false;
    Reader p{wire};std::string tag;SafetyTelemetryRequest r;uint64_t pid=0,enabled=0,budget=0,count=0;
    if(!p.line(tag)||tag!="CMSTQ1"||!p.number(pid,UINT32_MAX)||!pid||!p.number(enabled,1)||!p.number(r.epoch)||!p.number(r.revision)||
        !p.instant(r.issued)||!p.number(budget,1000)||!p.number(count,safetyTelemetryDeviceLimit))return false;
    r.enabled=enabled!=0;r.collectionBudgetMs=static_cast<unsigned>(budget);
    for(uint64_t i=0;i<count;++i){std::string d;if(!p.line(d,40))return false;r.devices.push_back(std::move(d));}
    if(p.at!=wire.size()||!validSafetyTelemetryRequest(r))return false;
    *out=std::move(r);*parent=pid;return true;
}
bool encodeSafetyTelemetryReply(const SafetyTelemetryRequest &r,const SafetyTelemetryReply &v,std::string *wire){
    if(!wire||!validReply(r,v))return false;
    auto s=std::string("CMSTA1\n")+binding(r)+"\n"+std::to_string(static_cast<unsigned>(v.status))+"\n";
    instant(s,v.began);instant(s,v.finished);s+=std::to_string(v.readings.size())+"\n";
    for(const auto &d:v.readings)s+=std::to_string(d.temperatureC+273)+"\n"+std::to_string(d.activeClockEvents)+"\n"+std::to_string(d.supportedClockEvents)+"\n";
    if(s.size()>safetyTelemetryWireLimit)return false;
    *wire=std::move(s);return true;
}
bool decodeSafetyTelemetryReply(const SafetyTelemetryRequest &r,const std::string &wire,SafetyTelemetryReply *out){
    if(!out||wire.size()>safetyTelemetryWireLimit||!validSafetyTelemetryRequest(r))return false;
    Reader p{wire};std::string tag,hash;uint64_t status=0,count=0;SafetyTelemetryReply v;
    if(!p.line(tag)||tag!="CMSTA1"||!p.line(hash)||hash!=binding(r)||!p.number(status,static_cast<unsigned>(SafetyTelemetryStatus::Invalid))||
        !p.instant(v.began)||!p.instant(v.finished)||!p.number(count,safetyTelemetryDeviceLimit))return false;
    v.status=static_cast<SafetyTelemetryStatus>(status);
    for(uint64_t i=0;i<count;++i){uint64_t temp=0;SafetyTelemetryReading d;
        if(!p.number(temp,523)||!p.number(d.activeClockEvents)||!p.number(d.supportedClockEvents))return false;
        d.temperatureC=static_cast<int>(temp)-273;v.readings.push_back(d);}
    if(p.at!=wire.size()||!validReply(r,v))return false;
    *out=std::move(v);return true;
}
SafetyTelemetryResult SafetyTelemetryClient::sample(const SafetyTelemetryRequest &r,const std::atomic<bool> &cancel,unsigned timeout){
    SafetyTelemetryResult out;std::string wire;
    if(timeout<50||timeout>2000||timeout<r.collectionBudgetMs||!encodeSafetyTelemetryRequest(r,localTaskProcessId(),&wire)){out.transport=LocalTaskStatus::Invalid;return out;}
    if(cancel.load()){out.transport=LocalTaskStatus::Cancelled;return out;}
    if(!r.enabled){out.transport=LocalTaskStatus::Ok;out.reply=SafetyTelemetryReply{SafetyTelemetryStatus::Disabled,r.issued,r.issued,{}};return out;}
    const auto began=safetyTelemetryTime();
    if(!ordered(r.issued,began)||began.utcMs-r.issued.utcMs>=timeout||began.monotonicMs-r.issued.monotonicMs>=timeout){out.transport=LocalTaskStatus::Invalid;return out;}
    const auto result=process_.run(wire,cancel,timeout);out.transport=result.status;
    if(out.transport!=LocalTaskStatus::Ok)return out;
    SafetyTelemetryReply reply;
    if(!decodeSafetyTelemetryReply(r,result.response,&reply))out.transport=LocalTaskStatus::Unavailable;
    else if(cancel.load())out.transport=LocalTaskStatus::Cancelled;
    else{const auto now=safetyTelemetryTime();
        if(!ordered(began,now))out.transport=LocalTaskStatus::Unavailable;
        else if(now.monotonicMs-began.monotonicMs>=timeout||now.utcMs-began.utcMs>=timeout)out.transport=LocalTaskStatus::Timeout;
        else if(reply.status==SafetyTelemetryStatus::Ok&&(!ordered(began,reply.began)||!ordered(reply.finished,now)))out.transport=LocalTaskStatus::Unavailable;
        else out.reply=std::move(reply);}
    return out;
}
} // namespace om
