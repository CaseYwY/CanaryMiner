#include "energy_compaction.h"
#include "hash.h"
#include "json_dom.h"
#include <algorithm>

namespace om::energy_history_detail {
namespace {
using S=EnergyHistoryStatus;
bool array(const JsonValue &v,size_t n){return v.isArray()&&v.items.size()==n;}
bool u(const JsonValue &v,uint64_t &n,uint64_t max=INT64_MAX){return v.asUint64(&n)&&n<=max;}
bool time(const JsonValue &v,int64_t &n){uint64_t x=0;if(!u(v,x,lastUtcMs))return false;n=static_cast<int64_t>(x);return true;}
bool id(const JsonValue &v,std::string &s,bool empty=false){if(!v.isString()||(!identifier(v.text)&&!(empty&&v.text.empty())))return false;s=v.text;return true;}
bool hash(const std::string &s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool boundedSnapshotJson(const std::string &text){
    // Separate local checkpoint envelope. Do not relax the much smaller public/provider/IPC JSON guard.
    if(text.empty()||text.size()>energySnapshotLimit)return false;
    size_t depth=0,nodes=0,stringBytes=0,literalBytes=0;bool quoted=false,escaped=false;
    for(unsigned char c:text){
        if(c>126||(c<32&&c!='\n'&&c!='\r'&&c!='\t'))return false;
        if(quoted){if(c<32||++stringBytes>256)return false;
            if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}
        if(c=='"'){quoted=true;stringBytes=0;literalBytes=0;++nodes;}
        else if(c=='['){if(++depth>16)return false;literalBytes=0;++nodes;}
        else if(c==']'){if(!depth)return false;--depth;literalBytes=0;}
        else if(c=='{'||c=='}'||c==':')return false;
        else if(c==','||c==' '||c=='\r'||c=='\n'||c=='\t')literalBytes=0;
        else{if(!literalBytes)++nodes;if(++literalBytes>32)return false;}
        if(nodes>250000)return false;
    }
    return !quoted&&!depth;
}
EnergyHistoryBatch carrier(const EnergyHistoryCheckpoint &h){EnergyHistoryBatch b;b.sequence=h.sequence;b.id=h.batchId;b.observedAtMs=h.observedAtMs;return b;}
std::string snapshotText(const EnergyCompactedSnapshot &s){
    const auto &m=s.model;const auto &h=m.head;
    std::string text="[\"CMECS1\",["+std::to_string(h.sequence)+","+std::to_string(h.readingCount)+","+std::to_string(h.sessionCount)+","+
        std::to_string(h.windowCount)+","+jsonQuote(h.batchId)+","+jsonQuote(h.sessionId)+","+std::to_string(h.observedAtMs)+","+std::to_string(h.throughUtcMs)+"],"+
        jsonQuote(s.priorDigest)+","+jsonQuote(s.digest)+","+batchText(s.original)+",";
    if(m.session){auto b=carrier(h);b.start=m.session;b.sessionId=m.session->id;for(const auto &c:m.channels)if(c.last)b.readings.push_back(*c.last);text+=batchText(b);}else text+="null";
    text+=","+std::to_string(m.lastMono)+","+std::to_string(m.lastWall)+","+std::to_string(m.lastContinuity)+",[";
    bool comma=false;for(const auto &key:m.sessions){if(comma)text+=',';text+=jsonQuote(key);comma=true;}
    text+="],[";auto windows=carrier(h);comma=false;
    for(auto i=m.windows.begin();i!=m.windows.end();){windows.windows.push_back(i->second);++i;
        if(windows.windows.size()==32||i==m.windows.end()){if(comma)text+=',';text+=batchText(windows);comma=true;windows.windows.clear();}}
    text+="],[";comma=false;for(const auto &i:m.segments){if(comma)text+=',';comma=true;
        text+="["+std::to_string(i.begin)+","+std::to_string(i.end)+","+std::to_string(i.milliwatts)+","+std::to_string(i.coveredChannels)+"]";}
    return text+"]]";
}
}
std::string retentionText(const EnergyHistoryLimits &l){
    if(!l.compaction)return {};
    return "[\"CMEP1\","+std::to_string(l.maxBatches)+","+std::to_string(l.maxReadings)+","+std::to_string(l.maxSessions)+","+
        std::to_string(l.maxWindows)+","+std::to_string(l.maxDatabaseBytes)+","+std::to_string(l.compaction->afterBatches)+","+std::to_string(l.compaction->maxSegments)+"]";
}
S compactEnergyModel(const EnergyHistoryBinding &b,const EnergyHistoryLimits &l,Model *model){
    if(!model||!validEnergyHistoryBinding(b)||!validEnergyHistoryLimits(l)||!l.compaction)return S::Invalid;
    struct Delta{int64_t mw=0,count=0;};std::map<int64_t,Delta> events;
    const auto add=[&](int64_t a,int64_t z,uint64_t mw,unsigned count){
        if(a<b.historyBeginUtcMs||z<=a||z>model->head.throughUtcMs||!count||count>b.channels.size()||mw>static_cast<uint64_t>(count)*UINT32_MAX)return false;
        events[a].mw+=static_cast<int64_t>(mw);events[a].count+=count;events[z].mw-=static_cast<int64_t>(mw);events[z].count-=count;return true;
    };
    if(model->segments.size()>l.compaction->maxSegments||model->intervals.size()>l.maxReadings+128)return S::Capacity;
    for(const auto &i:model->segments)if(!add(i.begin,i.end,i.milliwatts,i.coveredChannels))return S::Invalid;
    for(const auto &i:model->intervals)if(i.channel>=b.channels.size()||!add(i.begin,i.end,i.milliwatts,1))return S::Invalid;
    std::vector<EnergySegment> merged;int64_t mw=0,count=0,prior=0;
    for(const auto &e:events){
        if(count&&prior<e.first){
            if(!merged.empty()&&merged.back().end==prior&&merged.back().milliwatts==static_cast<uint64_t>(mw)&&merged.back().coveredChannels==static_cast<unsigned>(count))merged.back().end=e.first;
            else{if(merged.size()>=l.compaction->maxSegments)return S::Capacity;merged.push_back({prior,e.first,static_cast<uint64_t>(mw),static_cast<unsigned>(count)});}
        }
        mw+=e.second.mw;count+=e.second.count;
        if(count<0||count>static_cast<int64_t>(b.channels.size())||mw<0||static_cast<uint64_t>(mw)>static_cast<uint64_t>(count)*UINT32_MAX)return S::Invalid;
        prior=e.first;
    }
    if(mw||count)return S::Invalid;
    model->segments=std::move(merged);model->intervals.clear();model->head.compactedSequence=model->head.sequence;return S::Ok;
}
bool decodeEnergySnapshot(const EnergyHistoryBinding &b,const EnergyHistoryLimits &l,const std::string &text,EnergyCompactedSnapshot *out){
    if(!out||!validEnergyHistoryBinding(b)||!validEnergyHistoryLimits(l)||!l.compaction||!boundedSnapshotJson(text))return false;
    // The common public entry point caps documents at 1 MiB. This private 4-MiB envelope has already
    // applied its own byte/node/string/depth bounds; reuse the strict grammar without changing that public cap.
    json_dom_detail::Reader reader(text);JsonValue v;if(!reader.value(&v,0))return false;reader.ws();
    if(reader.p!=text.size()||!array(v,12)||!v.items[0].isString()||v.items[0].text!="CMECS1")return false;
    const auto &a=v.items;EnergyCompactedSnapshot s;auto &m=s.model;auto &h=m.head;
    if(!array(a[1],8))return false;
    const auto &head=a[1].items;
    if(!u(head[0],h.sequence,lifetimeBatchLimit)||!h.sequence||!u(head[1],h.readingCount,128*h.sequence)||!u(head[2],h.sessionCount,l.maxSessions)||
        !u(head[3],h.windowCount,l.maxWindows)||!id(head[4],h.batchId)||h.batchId!="energy."+std::to_string(h.sequence)||!id(head[5],h.sessionId,true)||
        !time(head[6],h.observedAtMs)||h.observedAtMs<b.historyBeginUtcMs||!time(head[7],h.throughUtcMs)||h.throughUtcMs>h.observedAtMs||
        h.sessionCount>h.sequence||h.windowCount>32*h.sequence||(!h.sessionCount&&!h.windowCount))return false;
    if(!a[2].isString()||!hash(a[2].text)||!a[3].isString()||!hash(a[3].text))return false;
    s.priorDigest=a[2].text;s.digest=a[3].text;
    if(!parseBatchValue(a[4],b,&s.original)||s.original.sequence!=h.sequence||s.original.id!=h.batchId||s.original.observedAtMs!=h.observedAtMs||
        s.digest!=sha256Hex(s.priorDigest+"|"+batchText(s.original))||
        (h.sequence==1&&s.priorDigest!=sha256Hex(bindingText(b))))return false;
    if(!u(a[6],m.lastMono)||!time(a[7],m.lastWall)||!u(a[8],m.lastContinuity)||!a[9].isArray()||a[9].items.size()!=h.sessionCount||
        !a[10].isArray()||a[10].items.size()>(l.maxWindows+31)/32||!a[11].isArray()||a[11].items.size()>l.compaction->maxSegments)return false;
    for(const auto &key:a[9].items){std::string value;if(!id(key,value)||!m.sessions.insert(value).second)return false;}
    if(a[5].type!=JsonValue::Type::Null){EnergyHistoryBatch tail;
        if(!parseBatchValue(a[5],b,&tail)||!tail.start||tail.sequence!=h.sequence||tail.id!=h.batchId||tail.observedAtMs!=h.observedAtMs||!tail.windows.empty()||
            tail.sessionId!=h.sessionId||!m.sessions.count(tail.sessionId))return false;
        m.session=tail.start;const auto &session=*m.session;
        if(m.lastMono<session.anchorMonoMs||m.lastMono-session.anchorMonoMs>static_cast<uint64_t>(lastUtcMs-session.anchorUtcMs)||
            session.anchorUtcMs+static_cast<int64_t>(m.lastMono-session.anchorMonoMs)!=h.throughUtcMs||m.lastWall<session.anchorUtcMs||m.lastWall>h.observedAtMs||
            static_cast<uint64_t>(m.lastWall>h.throughUtcMs?m.lastWall-h.throughUtcMs:h.throughUtcMs-m.lastWall)>clockGuardMs)return false;
        m.channels.resize(b.channels.size());
        for(const auto &r:tail.readings){
            if(m.channels[r.channel].last||r.monotonicMs<session.anchorMonoMs||r.monotonicMs>m.lastMono||r.utcMs<session.anchorUtcMs||r.utcMs>m.lastWall||r.continuity>m.lastContinuity)return false;
            const auto mapped=session.anchorUtcMs+static_cast<int64_t>(r.monotonicMs-session.anchorMonoMs);
            if(static_cast<uint64_t>(r.utcMs>mapped?r.utcMs-mapped:mapped-r.utcMs)>clockGuardMs)return false;
            m.channels[r.channel].last=r;
        }
        if(tail.readings.size()>h.readingCount)return false;
    }else if(h.sessionCount||!h.sessionId.empty()||h.readingCount||h.throughUtcMs||m.lastMono||m.lastWall||m.lastContinuity)return false;
    for(const auto &group:a[10].items){EnergyHistoryBatch batch;
        if(!parseBatchValue(group,b,&batch)||batch.start||!batch.readings.empty()||!batch.sessionId.empty()||batch.sequence!=h.sequence||batch.id!=h.batchId||batch.observedAtMs!=h.observedAtMs)return false;
        for(const auto &w:batch.windows){if(!m.windows.emplace(std::make_pair(w.registerId,w.periodId),w).second)return false;}
    }
    if(m.windows.size()!=h.windowCount)return false;
    std::map<std::string,std::vector<std::pair<int64_t,int64_t>>> windows;
    for(const auto &w:m.windows)windows[w.second.registerId].push_back({w.second.beginUtcMs,w.second.endUtcMs});
    for(auto &reg:windows){auto &rows=reg.second;std::sort(rows.begin(),rows.end());for(size_t i=1;i<rows.size();++i)if(rows[i].first<rows[i-1].second)return false;}
    uint64_t covered=0;
    for(const auto &row:a[11].items){EnergySegment i;uint64_t count=0;
        if(!array(row,4)||!time(row.items[0],i.begin)||!time(row.items[1],i.end)||i.begin<b.historyBeginUtcMs||i.end<=i.begin||i.end>h.throughUtcMs||
            !u(row.items[3],count,b.channels.size())||!count||!u(row.items[2],i.milliwatts,count*UINT32_MAX))return false;
        i.coveredChannels=static_cast<unsigned>(count);
        if(!m.segments.empty()){const auto &prev=m.segments.back();if(i.begin<prev.end||(i.begin==prev.end&&i.milliwatts==prev.milliwatts&&i.coveredChannels==prev.coveredChannels))return false;}
        covered+=static_cast<uint64_t>(i.end-i.begin)*count;m.segments.push_back(i);
    }
    if(covered>h.readingCount*60000)return false;
    if((s.original.start||!s.original.readings.empty())&&s.original.sessionId!=h.sessionId)return false;
    std::map<unsigned,EnergyHistoryReading> originalLast;for(const auto &r:s.original.readings)originalLast[r.channel]=r;
    for(const auto &entry:originalLast){const auto &r=entry.second;const auto &last=m.channels[r.channel].last;
        if(!last||last->utcMs!=r.utcMs||last->monotonicMs!=r.monotonicMs||last->continuity!=r.continuity||last->milliwatts!=r.milliwatts)return false;}
    if(s.original.start){const auto &start=*s.original.start;
        if(!m.session||m.session->id!=start.id||m.session->anchorUtcMs!=start.anchorUtcMs||m.session->anchorMonoMs!=start.anchorMonoMs||m.session->maximumGapMs!=start.maximumGapMs)return false;}
    for(const auto &w:s.original.windows){const auto it=m.windows.find({w.registerId,w.periodId});if(it==m.windows.end())return false;
        auto one=carrier(h),two=one;one.windows={w};two.windows={it->second};if(batchText(one)!=batchText(two))return false;}
    h.status=S::Ok;h.compactedSequence=h.sequence;m.batchIds.insert(h.batchId);
    if(snapshotText(s)!=text)return false;
    *out=std::move(s);return true;
}
bool encodeEnergySnapshot(const EnergyHistoryBinding &b,const EnergyHistoryLimits &l,const EnergyCompactedSnapshot &s,std::string *out){
    if(!out||!s.model.intervals.empty()||s.model.head.compactedSequence!=s.model.head.sequence)return false;
    const auto text=snapshotText(s);EnergyCompactedSnapshot checked;if(!decodeEnergySnapshot(b,l,text,&checked))return false;*out=text;return true;
}
} // namespace om::energy_history_detail
