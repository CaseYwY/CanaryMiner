#include "energy_history_model.h"
#include "json_dom.h"
#include "public_https.h" // shared bounded JSON lexical guard only
#include <algorithm>

namespace om {
namespace energy_history_detail {
bool instant(int64_t n) { return n>0 && n<=lastUtcMs; }
bool identifier(const std::string &s) {
    return !s.empty() && s.size()<=128 && s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.:")==std::string::npos;
}
namespace {
bool array(const JsonValue &v,size_t n) { return v.isArray() && v.items.size()==n; }
bool u(const JsonValue &v,uint64_t &n,uint64_t max=INT64_MAX) { return v.asUint64(&n) && n<=max; }
bool time(const JsonValue &v,int64_t &n) { uint64_t value=0; if(!u(v,value,lastUtcMs))return false; n=static_cast<int64_t>(value);return true; }
bool str(const JsonValue &v,std::string &s) { if(!v.isString()||!identifier(v.text))return false;s=v.text;return true; }
bool emptyStr(const JsonValue &v,std::string &s) { if(!v.isString()||(!v.text.empty()&&!identifier(v.text)))return false;s=v.text;return true; }
bool windowValid(const EnergyResetWindow &w) {
    return identifier(w.registerId)&&identifier(w.periodId)&&identifier(w.ruleVersion)&&instant(w.beginUtcMs)&&instant(w.endUtcMs)&&
        w.endUtcMs>w.beginUtcMs&&w.endUtcMs-w.beginUtcMs<=370LL*86400000&&
        (w.fixedStandardTime?(w.offsetSeconds>=-64800&&w.offsetSeconds<=64800&&w.offsetSeconds%60==0):w.offsetSeconds==0);
}
ExactDecimal decimal(uint64_t n) { ExactDecimal d;ExactDecimal::parse(std::to_string(n),&d);return d; }
}
std::string bindingText(const EnergyHistoryBinding &b) {
    std::string text="[\"CMEH1\","+std::to_string(static_cast<unsigned>(b.evidence.basis))+","+std::to_string(static_cast<unsigned>(b.evidence.origin))+","+
        jsonQuote(b.evidence.scope)+","+jsonQuote(b.evidence.adapter)+","+std::to_string(b.historyBeginUtcMs)+",[";
    for(size_t i=0;i<b.channels.size();++i){if(i)text+=',';text+=jsonQuote(b.channels[i]);}
    return text+"]]";
}
bool parseBinding(const std::string &text,EnergyHistoryBinding *out) {
    if(!out||text.size()>16*1024||!boundedPublicJson(text))return false;
    JsonValue v;return parseJsonStrict(text,&v,nullptr)&&parseBindingValue(v,out);
}
bool parseBindingValue(const JsonValue &v,EnergyHistoryBinding *out) {
    if(!out||!array(v,7))return false;
    const auto &a=v.items;EnergyHistoryBinding b;uint64_t basis=0,origin=0;
    if(!a[0].isString()||a[0].text!="CMEH1"||!u(a[1],basis,4)||!u(a[2],origin,3)||!str(a[3],b.evidence.scope)||!str(a[4],b.evidence.adapter)||
        !time(a[5],b.historyBeginUtcMs)||!a[6].isArray()||a[6].items.size()>64)return false;
    b.evidence.basis=static_cast<PowerBasis>(basis);b.evidence.origin=static_cast<PowerOrigin>(origin);
    for(const auto &c:a[6].items){std::string s;if(!str(c,s))return false;b.channels.push_back(std::move(s));}
    if(!validEnergyHistoryBinding(b))return false;
    *out=std::move(b);return true;
}
std::string batchText(const EnergyHistoryBatch &b) {
    std::string text="[\"CMEB1\","+std::to_string(b.sequence)+","+jsonQuote(b.id)+","+std::to_string(b.observedAtMs)+",";
    if(b.start){const auto &s=*b.start;text+="["+jsonQuote(s.id)+","+std::to_string(s.anchorUtcMs)+","+std::to_string(s.anchorMonoMs)+","+std::to_string(s.maximumGapMs)+"]";}
    else text+="null";
    text+=","+jsonQuote(b.sessionId)+",[";
    for(size_t i=0;i<b.readings.size();++i){if(i)text+=',';const auto &r=b.readings[i];
        text+="["+std::to_string(r.channel)+","+std::to_string(r.utcMs)+","+std::to_string(r.monotonicMs)+","+std::to_string(r.continuity)+","+(r.milliwatts?std::to_string(*r.milliwatts):"null")+"]";}
    text+="],[";
    for(size_t i=0;i<b.windows.size();++i){if(i)text+=',';const auto &w=b.windows[i];
        text+="["+jsonQuote(w.registerId)+","+jsonQuote(w.periodId)+","+jsonQuote(w.ruleVersion)+","+std::to_string(w.beginUtcMs)+","+std::to_string(w.endUtcMs)+","+
            (w.fixedStandardTime?"true":"false")+","+jsonQuote(std::to_string(w.offsetSeconds))+"]";}
    return text+"]]";
}
bool parseBatch(const std::string &text,const EnergyHistoryBinding &binding,EnergyHistoryBatch *out) {
    if(!out||text.size()>batchWireLimit||!boundedPublicJson(text))return false;
    JsonValue v;return parseJsonStrict(text,&v,nullptr)&&parseBatchValue(v,binding,out);
}
bool parseBatchValue(const JsonValue &v,const EnergyHistoryBinding &binding,EnergyHistoryBatch *out) {
    if(!out||!array(v,8))return false;
    const auto &a=v.items;EnergyHistoryBatch b;
    if(!a[0].isString()||a[0].text!="CMEB1"||!u(a[1],b.sequence)||!str(a[2],b.id)||!time(a[3],b.observedAtMs)||
        !emptyStr(a[5],b.sessionId)||!a[6].isArray()||a[6].items.size()>128||!a[7].isArray()||a[7].items.size()>32)return false;
    if(a[4].type!=JsonValue::Type::Null){const auto &s=a[4];EnergyHistorySession start;uint64_t gap=0;
        if(!array(s,4)||!str(s.items[0],start.id)||!time(s.items[1],start.anchorUtcMs)||!u(s.items[2],start.anchorMonoMs)||!u(s.items[3],gap,60000))return false;
        start.maximumGapMs=static_cast<unsigned>(gap);b.start=std::move(start);}
    for(const auto &s:a[6].items){EnergyHistoryReading r;uint64_t channel=0,power=0;
        if(!array(s,5)||!u(s.items[0],channel,63)||!time(s.items[1],r.utcMs)||!u(s.items[2],r.monotonicMs)||!u(s.items[3],r.continuity))return false;
        r.channel=static_cast<unsigned>(channel);if(s.items[4].type!=JsonValue::Type::Null){if(!u(s.items[4],power,UINT32_MAX))return false;r.milliwatts=static_cast<uint32_t>(power);}
        b.readings.push_back(r);}
    for(const auto &s:a[7].items){EnergyResetWindow w;
        if(!array(s,7)||!str(s.items[0],w.registerId)||!str(s.items[1],w.periodId)||!str(s.items[2],w.ruleVersion)||!time(s.items[3],w.beginUtcMs)||
            !time(s.items[4],w.endUtcMs)||!s.items[5].isBool()||!s.items[6].isString())return false;
        w.fixedStandardTime=s.items[5].boolean;
        const auto &offset=s.items[6].text;if(offset.empty()||offset.size()>6)return false;
        try{size_t used=0;w.offsetSeconds=std::stoi(offset,&used);if(used!=offset.size()||std::to_string(w.offsetSeconds)!=offset)return false;}catch(...){return false;}
        b.windows.push_back(std::move(w));}
    if(!boundedEnergyHistoryBatch(binding,b,b.observedAtMs))return false;
    *out=std::move(b);return true;
}
EnergyHistoryStatus Model::advance(const EnergyHistoryBinding &binding,const EnergyHistoryBatch &b,const EnergyHistoryLimits &limits) {
    using S=EnergyHistoryStatus;
    if(b.sequence!=head.sequence+1)return S::SequenceGap;
    if(limits.compaction&&b.id!="energy."+std::to_string(b.sequence))return S::Invalid;
    if(batchIds.count(b.id))return S::Conflict;
    if(b.observedAtMs<head.observedAtMs)return S::Clock;
    if(head.sequence>=(limits.compaction?lifetimeBatchLimit:limits.maxBatches)||
        (limits.compaction?b.readings.size()>limits.maxReadings:b.readings.size()>limits.maxReadings-head.readingCount)||
        b.windows.size()>limits.maxWindows-head.windowCount)return S::Capacity;
    for(const auto &w:b.windows){
        const auto key=std::make_pair(w.registerId,w.periodId);if(windows.count(key))return S::Conflict;
        for(const auto &entry:windows){const auto &prior=entry.second;
            if(prior.registerId==w.registerId&&w.beginUtcMs<prior.endUtcMs&&prior.beginUtcMs<w.endUtcMs)return S::Conflict;}
        windows.emplace(key,w);
    }
    if(b.start){const auto &s=*b.start;
        if(sessions.count(s.id))return S::Conflict;
        if(sessions.size()>=limits.maxSessions)return S::Capacity;
        // Switching sessions seals the old monotonic domain. No final hold or startup/backdated fill.
        if(s.anchorUtcMs<binding.historyBeginUtcMs||s.anchorUtcMs<head.throughUtcMs||s.anchorUtcMs<lastWall)return S::Clock;
        session=s;sessions.insert(s.id);channels.assign(binding.channels.size(),{});lastMono=s.anchorMonoMs;lastWall=s.anchorUtcMs;lastContinuity=0;
        head.sessionId=s.id;head.throughUtcMs=s.anchorUtcMs;
    }
    if(!b.readings.empty()&&(!session||session->id!=b.sessionId))return S::Conflict;
    for(const auto &r:b.readings){const auto &s=*session;
        if(r.monotonicMs<s.anchorMonoMs||r.monotonicMs<lastMono||r.utcMs<lastWall||r.continuity<lastContinuity)return S::Clock;
        const auto elapsed=r.monotonicMs-s.anchorMonoMs;
        if(elapsed>static_cast<uint64_t>(lastUtcMs-s.anchorUtcMs))return S::Clock;
        const auto mapped=s.anchorUtcMs+static_cast<int64_t>(elapsed);
        if(mapped>b.observedAtMs||static_cast<uint64_t>(r.utcMs>mapped?r.utcMs-mapped:mapped-r.utcMs)>clockGuardMs)return S::Clock;
        auto &channel=channels[r.channel];
        if(channel.last){const auto &previous=*channel.last;
            if(r.monotonicMs<=previous.monotonicMs)return S::Clock;
            const auto dt=r.monotonicMs-previous.monotonicMs;
            if(r.continuity==previous.continuity&&previous.milliwatts&&dt<=s.maximumGapMs)
                intervals.push_back({mapped-static_cast<int64_t>(dt),mapped,r.channel,*previous.milliwatts});
        }
        channel.last=r;lastMono=r.monotonicMs;lastWall=r.utcMs;lastContinuity=r.continuity;head.throughUtcMs=mapped;
    }
    head.status=S::Ok;head.sequence=b.sequence;head.batchId=b.id;head.observedAtMs=b.observedAtMs;
    head.readingCount+=b.readings.size();head.sessionCount=sessions.size();head.windowCount=windows.size();
    if(limits.compaction)batchIds.clear(); // sequence-derived v2 IDs cannot be reused, including after raw expiry
    batchIds.insert(b.id);
    return S::Ok;
}
EnergyHistoryTotals Model::query(const EnergyHistoryBinding &binding,int64_t begin,int64_t end,int64_t now) const {
    EnergyHistoryTotals t;t.beginUtcMs=begin;t.endUtcMs=end;
    if(!instant(now)||!instant(begin)||!instant(end)||begin>=end||end>now){t.status=EnergyHistoryStatus::Invalid;return t;}
    if(now<head.observedAtMs){t.status=EnergyHistoryStatus::Clock;return t;}
    t.status=EnergyHistoryStatus::Ok;t.observedAtMs=head.observedAtMs;
    t.requestedDeviceMs=static_cast<uint64_t>(end-begin)*binding.channels.size();
    ExactDecimal milli;ExactDecimal::parse("0.001",&milli);
    for(const auto &i:segments){const auto a=(std::max)(begin,i.begin),z=(std::min)(end,i.end);if(a>=z)continue;
        const auto dt=static_cast<uint64_t>(z-a);ExactDecimal n;
        if(!decimal(i.milliwatts).multiply(decimal(dt),&n)||!n.multiply(milli,&n)||!t.knownKwh.numerator.add(n,&t.knownKwh.numerator)){
            EnergyHistoryTotals fail;fail.status=EnergyHistoryStatus::Capacity;fail.beginUtcMs=begin;fail.endUtcMs=end;return fail;}
        t.coveredDeviceMs+=dt*i.coveredChannels;
    }
    for(const auto &i:intervals){const auto a=(std::max)(begin,i.begin),z=(std::min)(end,i.end);if(a>=z)continue;
        const auto dt=static_cast<uint64_t>(z-a);ExactDecimal n;
        if(!decimal(i.milliwatts).multiply(decimal(dt),&n)||!n.multiply(milli,&n)||!t.knownKwh.numerator.add(n,&t.knownKwh.numerator)){
            EnergyHistoryTotals fail;fail.status=EnergyHistoryStatus::Capacity;fail.beginUtcMs=begin;fail.endUtcMs=end;return fail;}
        t.coveredDeviceMs+=dt;
    }
    t.coverage=t.coveredDeviceMs==t.requestedDeviceMs?EnergyHistoryCoverage::Complete:t.coveredDeviceMs?EnergyHistoryCoverage::Partial:EnergyHistoryCoverage::Unavailable;
    if(t.coverage==EnergyHistoryCoverage::Complete)t.completeKwh=t.knownKwh;
    return t;
}
} // namespace energy_history_detail
bool validEnergyHistoryBinding(const EnergyHistoryBinding &b) {
    using namespace energy_history_detail;
    if(!instant(b.historyBeginUtcMs)||!identifier(b.evidence.scope)||!identifier(b.evidence.adapter)||b.channels.empty()||b.channels.size()>64)return false;
    if(b.evidence.origin==PowerOrigin::SyntheticFixture){
        if(b.evidence.adapter.rfind("fixture.",0)!=0||b.evidence.basis==PowerBasis::Unknown||static_cast<unsigned>(b.evidence.basis)>4)return false;
    }else if(b.evidence.origin!=PowerOrigin::ObservedTelemetry||b.evidence.basis!=PowerBasis::GpuBoard||b.evidence.adapter!="native-nvml-milliwatts")return false;
    std::set<std::string> unique;for(const auto &c:b.channels)if(!identifier(c)||!unique.insert(c).second)return false;
    return true;
}
bool validEnergyHistoryLimits(const EnergyHistoryLimits &l) {
    return l.maxBatches&&l.maxBatches<=4096&&l.maxReadings&&l.maxReadings<=131072&&l.maxSessions&&l.maxSessions<=1024&&l.maxWindows&&l.maxWindows<=4096&&
        l.maxDatabaseBytes>=1024*1024&&l.maxDatabaseBytes<=128*1024*1024&&
        (!l.compaction||(l.compaction->afterBatches&&l.compaction->afterBatches<=l.maxBatches&&l.compaction->maxSegments&&l.compaction->maxSegments<=32768));
}
bool sameEnergyHistoryLimits(const EnergyHistoryLimits &a,const EnergyHistoryLimits &b){
    return a.maxBatches==b.maxBatches&&a.maxReadings==b.maxReadings&&a.maxSessions==b.maxSessions&&a.maxWindows==b.maxWindows&&
        a.maxDatabaseBytes==b.maxDatabaseBytes&&a.compaction.has_value()==b.compaction.has_value()&&
        (!a.compaction||(a.compaction->afterBatches==b.compaction->afterBatches&&a.compaction->maxSegments==b.compaction->maxSegments));
}
bool boundedEnergyHistoryBatch(const EnergyHistoryBinding &binding,const EnergyHistoryBatch &b,int64_t now) {
    using namespace energy_history_detail;
    if(!validEnergyHistoryBinding(binding)||!instant(now)||!b.sequence||b.sequence>INT64_MAX||!identifier(b.id)||!instant(b.observedAtMs)||
        b.observedAtMs>now||b.observedAtMs<binding.historyBeginUtcMs||b.readings.size()>128||b.windows.size()>32||(!b.start&&b.readings.empty()&&b.windows.empty()))return false;
    if(b.start){const auto &s=*b.start;if(!identifier(s.id)||s.id!=b.sessionId||!instant(s.anchorUtcMs)||s.anchorUtcMs>b.observedAtMs||
        s.anchorUtcMs<binding.historyBeginUtcMs||s.anchorMonoMs>INT64_MAX||!s.maximumGapMs||s.maximumGapMs>60000)return false;}
    if(b.start||!b.readings.empty()){if(!identifier(b.sessionId))return false;}else if(!b.sessionId.empty())return false;
    for(const auto &r:b.readings)if(r.channel>=binding.channels.size()||!instant(r.utcMs)||r.utcMs>b.observedAtMs||r.monotonicMs>INT64_MAX||r.continuity>INT64_MAX)return false;
    for(const auto &w:b.windows)if(!windowValid(w))return false;
    return batchText(b).size()<=batchWireLimit;
}
const char *energyHistoryStatusName(EnergyHistoryStatus s) {
    switch(s){
        case EnergyHistoryStatus::Ok:return "ok";
        case EnergyHistoryStatus::Replay:return "exact committed batch replay";
        case EnergyHistoryStatus::Invalid:return "invalid or out-of-bound energy input";
        case EnergyHistoryStatus::Attribution:return "power source/channel binding mismatch";
        case EnergyHistoryStatus::Conflict:return "conflicting identity/session/reset window";
        case EnergyHistoryStatus::SequenceGap:return "energy batch sequence gap";
        case EnergyHistoryStatus::Clock:return "energy clock/continuity fault";
        case EnergyHistoryStatus::Capacity:return "energy history capacity";
        case EnergyHistoryStatus::Storage:return "energy storage/integrity unavailable; preserve history";
        case EnergyHistoryStatus::DetailExpired:return "original energy batch detail expired; aggregate retained";
    }return "unsupported energy history state";
}
} // namespace om
