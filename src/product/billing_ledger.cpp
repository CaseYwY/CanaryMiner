#include "billing_ledger.h"
#include "hash.h"
#include "json_dom.h"
#include "local_store_lease.h"
#include "sqlite_runtime.h"
#include <algorithm>
#include <cstdlib>
namespace om {
namespace {
constexpr int64_t lastUtc=253402300799999LL,application=0x434d424c;
constexpr size_t bindingLimit=1024*1024,chunkLimit=128*1024,maxChunkIntervals=512;
const std::string metaSql="CREATE TABLE meta(id INTEGER PRIMARY KEY CHECK(id=1),binding TEXT NOT NULL,sequence INTEGER NOT NULL,digest TEXT NOT NULL,state TEXT NOT NULL)";
const std::string eventsSql="CREATE TABLE events(sequence INTEGER PRIMARY KEY,id TEXT NOT NULL UNIQUE,body TEXT NOT NULL,digest TEXT NOT NULL,state TEXT NOT NULL)";
bool instant(int64_t t){return t>0&&t<=lastUtc;}
bool key(const std::string &s){return !s.empty()&&s.size()<=128&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=32&&c!=127;});}
std::string n(int64_t v){return std::to_string(v);}
std::string decimal(const ExactDecimal &v){return jsonQuote(v.text());}
std::string optional(const std::optional<ExactDecimal> &v){return v?decimal(*v):"null";}
std::string evidence(const PowerEvidence &e){return "["+n(static_cast<int>(e.basis))+","+n(static_cast<int>(e.origin))+","+jsonQuote(e.scope)+","+jsonQuote(e.adapter)+"]";}
bool sameEvidence(const PowerEvidence &a,const PowerEvidence &b){return a.basis==b.basis&&a.origin==b.origin&&a.scope==b.scope&&a.adapter==b.adapter;}
bool boundedPlan(const TariffPlan &p){
    if(!key(p.key)||!key(p.territory)||!key(p.supplier)||!key(p.product)||p.currency.size()!=3||p.clock.zone.size()>128||p.revisions.size()>128)return false;
    size_t rows=0;
    for(const auto &r:p.revisions){
        if(!key(r.key)||r.calendar.size()>64||r.bands.size()>64||r.dynamic.size()>4096)return false;
        rows+=r.calendar.size()+r.bands.size()+r.dynamic.size();if(rows>4096)return false;
        for(const auto &c:r.calendar)if(!key(c.key))return false;
    }
    return true;
}
bool validLimits(const BillingLimits &l){return l.maxBatches&&l.maxBatches<=4096&&l.maxIntervals&&l.maxIntervals<=131072&&l.maxDatabaseBytes>=1024*1024&&l.maxDatabaseBytes<=128*1024*1024;}
BillOpening initial(const BillingBinding &b){BillOpening s;s.declared=true;s.planKey=b.plan.key;s.evidence=b.evidence;s.cycleBeginUtcMs=s.throughUtcMs=b.cycleBeginUtcMs;s.cycleEndUtcMs=b.cycleEndUtcMs;return s;}
std::string planText(const TariffPlan &p){
    std::string s="["+n(p.declared)+","+n(static_cast<int>(p.origin))+","+jsonQuote(p.key)+","+jsonQuote(p.territory)+","+jsonQuote(p.supplier)+","+jsonQuote(p.product)+","+jsonQuote(p.currency)+
        ",["+n(static_cast<int>(p.clock.kind))+","+jsonQuote(p.clock.zone)+","+n(p.clock.fixedOffsetSeconds)+"],"+n(static_cast<int>(p.tiers))+","+n(static_cast<int>(p.rounding))+","+decimal(p.cycleCharge)+","+optional(p.minimumCycleSubtotal)+","+optional(p.subtotalMultiplier)+","+n(p.unsupportedRules)+",[";
    for(size_t i=0;i<p.revisions.size();++i){const auto &r=p.revisions[i];if(i)s+=',';
        s+="["+jsonQuote(r.key)+","+n(r.beginUtcMs)+","+n(r.endUtcMs)+","+n(r.publishedUtcMs)+","+n(static_cast<int>(r.timeMode))+","+decimal(r.baseRatePerKwh)+","+decimal(r.chargePerCivilDay)+",[";
        for(size_t j=0;j<r.calendar.size();++j){const auto &c=r.calendar[j];if(j)s+=',';
            s+="["+jsonQuote(c.key)+","+n(c.beginMinute)+","+n(c.endMinute)+","+n(c.weekdayMask)+","+n(c.monthMask)+","+n(c.priority)+","+
                (c.date?"["+n(c.date->year)+","+n(c.date->month)+","+n(c.date->day)+"]":"null")+","+n(c.publishedUtcMs)+","+decimal(c.ratePerKwh)+"]";}
        s+="],[";
        for(size_t j=0;j<r.dynamic.size();++j){const auto &d=r.dynamic[j];if(j)s+=',';s+="["+n(d.beginUtcMs)+","+n(d.endUtcMs)+","+n(d.publishedUtcMs)+","+decimal(d.ratePerKwh)+"]";}
        s+="],[";
        for(size_t j=0;j<r.bands.size();++j){if(j)s+=',';s+="["+optional(r.bands[j].throughKwh)+","+decimal(r.bands[j].ratePerKwh)+"]";}
        s+="]]";
    }
    return s+"]]";
}
std::string bindingText(const BillingBinding &b,const BillingLimits &l){return "[\"CMBL1\","+jsonQuote(b.cycleKey)+","+n(b.cycleBeginUtcMs)+","+n(b.cycleEndUtcMs)+","+evidence(b.evidence)+","+planText(b.plan)+
    ",["+n(static_cast<int64_t>(l.maxBatches))+","+n(static_cast<int64_t>(l.maxIntervals))+","+n(static_cast<int64_t>(l.maxDatabaseBytes))+"]]";}
bool boundedChunk(const BillingChunk &b){return b.sequence&&b.sequence<=4096&&key(b.id)&&instant(b.observedUtcMs)&&!b.trace.intervals.empty()&&b.trace.intervals.size()<=maxChunkIntervals&&key(b.trace.evidence.scope)&&key(b.trace.evidence.adapter);}
std::string chunkText(const BillingChunk &b){
    std::string s="[\"CMBC1\","+n(static_cast<int64_t>(b.sequence))+","+jsonQuote(b.id)+","+n(b.observedUtcMs)+","+evidence(b.trace.evidence)+",[";
    for(size_t i=0;i<b.trace.intervals.size();++i){if(i)s+=',';const auto &p=b.trace.intervals[i];s+="["+n(p.beginUtcMs)+","+n(p.endUtcMs)+","+decimal(p.watts)+"]";}
    return s+"]]";
}
bool array(const JsonValue &v,size_t count){return v.isArray()&&v.items.size()==count;}
bool integer(const JsonValue &v,int64_t &out){uint64_t value=0;if(!v.asUint64(&value)||value>static_cast<uint64_t>(lastUtc))return false;out=static_cast<int64_t>(value);return true;}
bool parseChunk(const std::string &s,const PowerEvidence &e,BillingChunk &out){
    if(s.size()>chunkLimit)return false;
    JsonValue root;if(!parseJsonStrict(s,&root,nullptr)||!array(root,6))return false;
    const auto &a=root.items;BillingChunk b;b.trace.evidence=e;
    if(!a[0].isString()||a[0].text!="CMBC1"||!a[1].asUint64(&b.sequence)||!a[2].isString()||!integer(a[3],b.observedUtcMs)||!array(a[4],4)||!a[5].isArray()||a[5].items.size()>maxChunkIntervals)return false;
    b.id=a[2].text;
    for(const auto &row:a[5].items){PowerInterval i;if(!array(row,3)||!integer(row.items[0],i.beginUtcMs)||!integer(row.items[1],i.endUtcMs)||!row.items[2].isString()||!ExactDecimal::parse(row.items[2].text,&i.watts))return false;b.trace.intervals.push_back(std::move(i));}
    // Canonical comparison also verifies every evidence field and excludes alternative/unknown JSON representations.
    if(!boundedChunk(b)||chunkText(b)!=s)return false;
    out=std::move(b);return true;
}
std::string stateText(const BillingSnapshot &s){return "["+n(static_cast<int64_t>(s.sequence))+","+n(static_cast<int64_t>(s.intervals))+","+jsonQuote(s.batchId)+","+n(s.observedUtcMs)+","+n(s.opening.throughUtcMs)+","+
    jsonQuote(s.opening.timeDatabaseVersion)+","+decimal(s.opening.importedKwh.numerator)+","+decimal(s.opening.accruedSubtotal.numerator)+","+
    (s.provisionalTotal?decimal(s.provisionalTotal->numerator):"null")+","+(s.settledTotal?decimal(s.settledTotal->numerator):"null")+","+n(s.settlementDependent)+"]";}
BillingStatus advance(const BillingBinding &binding,const BillingLimits &limits,const BillingChunk &chunk,BillingSnapshot &state){
    if(!boundedChunk(chunk))return BillingStatus::Invalid;
    if(!sameEvidence(binding.evidence,chunk.trace.evidence))return BillingStatus::Attribution;
    if(chunk.sequence!=state.sequence+1)return chunk.sequence>state.sequence?BillingStatus::SequenceGap:BillingStatus::Conflict;
    if(chunk.sequence>limits.maxBatches||state.intervals+chunk.trace.intervals.size()>limits.maxIntervals)return BillingStatus::Capacity;
    if(chunk.observedUtcMs<state.observedUtcMs||chunk.trace.intervals.back().endUtcMs>chunk.observedUtcMs)return BillingStatus::Clock;
    const auto step=accrueBill(binding.plan,state.opening,chunk.trace,chunk.trace.intervals.back().endUtcMs,chunk.observedUtcMs);
    if(step.status!=ElectricityStatus::Ok)return step.status==ElectricityStatus::Capacity?BillingStatus::Capacity:BillingStatus::Calculation;
    auto next=state;next.sequence=chunk.sequence;next.intervals+=chunk.trace.intervals.size();next.observedUtcMs=chunk.observedUtcMs;next.batchId=chunk.id;
    next.opening=*step.ending;next.provisionalTotal=step.provisionalTotal;next.settledTotal=step.settledTotal;next.settlementDependent=step.settlementDependent;
    state=std::move(next);return BillingStatus::Ok;
}
struct Param {bool isText=false;int64_t number=0;std::string value;Param(int64_t n):number(n){}Param(const std::string &s):isText(true),value(s){}};
} // namespace
struct BillingLedger::Impl {
    SqliteRuntime api;SqliteDb *db=nullptr;LocalStoreLease lease;
    bool attempted=false,ready=false,readOnly=false;
    int lastCode=0;
    BillingBinding binding;BillingLimits limits;BillingSnapshot current;std::string bound;
#ifdef CANARY_BILLING_TESTING
    bool crash=false;
#endif
    ~Impl(){if(db)api.closeV2(db);}
    struct Statement {
        Impl *owner;SqliteStatement *value=nullptr;
        Statement(Impl *s,const char *sql,std::initializer_list<Param> params={}):owner(s){
            owner->lastCode=owner->api.prepare(owner->db,sql,-1,&value,nullptr);
            if(owner->lastCode!=0){if(value)owner->api.finalize(value);value=nullptr;return;}
            int i=0;for(const auto &p:params){const int code=p.isText?owner->api.bindText(value,++i,p.value.data(),static_cast<int>(p.value.size()),reinterpret_cast<void(*)(void*)>(static_cast<intptr_t>(-1))):owner->api.bindInteger(value,++i,p.number);
                if(code){owner->lastCode=code;owner->api.finalize(value);value=nullptr;return;}}
        }
        ~Statement(){if(value)owner->api.finalize(value);}
        int step(){if(value)owner->lastCode=owner->api.step(value);else if(!owner->lastCode)owner->lastCode=1;return owner->lastCode;}
    };
    bool exec(const std::string &sql){lastCode=api.execute(db,sql.c_str(),nullptr,nullptr,nullptr);return lastCode==0;}
    bool write(const char *sql,std::initializer_list<Param> params={}){Statement q(this,sql,params);return q.step()==101;}
    bool number(SqliteStatement *q,int col,int64_t &out){if(api.columnType(q,col)!=1)return false;out=api.columnInteger(q,col);return true;}
    bool text(SqliteStatement *q,int col,std::string &out,size_t cap=bindingLimit){
        if(api.columnType(q,col)!=3)return false;
        const auto *p=api.columnText(q,col);const int bytes=api.columnBytes(q,col);
        if(!p||bytes<0||static_cast<size_t>(bytes)>cap||std::find(p,p+bytes,0)!=p+bytes)return false;
        out.assign(reinterpret_cast<const char*>(p),static_cast<size_t>(bytes));return true;
    }
    bool scalar(const char *sql,int64_t &out){Statement q(this,sql);return q.step()==100&&number(q.value,0,out)&&q.step()==101;}
    bool isText(const char *sql,const std::string &expected){Statement q(this,sql);std::string v;return q.step()==100&&text(q.value,0,v)&&v==expected&&q.step()==101;}
    BillingStatus fail(int code=0){ready=false;return code==13?BillingStatus::Capacity:BillingStatus::Storage;}
    bool audit(){
        int64_t count=0,sequence=0;std::string bindingText,head,ending;
        if(!scalar("SELECT count(*) FROM meta",count)||count!=1)return false;
        {Statement q(this,"SELECT binding,sequence,digest,state FROM meta WHERE id=1");
            if(q.step()!=100||!text(q.value,0,bindingText)||bindingText!=bound||!number(q.value,1,sequence)||sequence<0||static_cast<uint64_t>(sequence)>limits.maxBatches||
                !text(q.value,2,head,64)||!text(q.value,3,ending,2048)||q.step()!=101)return false;}
        BillingSnapshot checked;checked.status=BillingStatus::Ok;checked.opening=initial(binding);checked.digest=sha256Hex(bound);
        Statement q(this,"SELECT sequence,id,body,digest,state FROM events ORDER BY sequence");int code=0;
        while((code=q.step())==100){int64_t seq=0;std::string id,body,digest,saved;BillingChunk chunk;
            if(!number(q.value,0,seq)||seq<=0||seq>sequence||!text(q.value,1,id,512)||!text(q.value,2,body,chunkLimit)||!text(q.value,3,digest,64)||!text(q.value,4,saved,2048)||
                !parseChunk(body,binding.evidence,chunk)||chunk.sequence!=static_cast<uint64_t>(seq)||chunk.id!=id)return false;
            const auto previous=checked.digest;
            if(advance(binding,limits,chunk,checked)!=BillingStatus::Ok||saved!=stateText(checked)||digest!=sha256Hex(previous+"|"+body+"|"+saved))return false;
            checked.digest=std::move(digest);
        }
        if(code!=101||checked.sequence!=static_cast<uint64_t>(sequence)||head!=checked.digest||ending!=stateText(checked))return false;
        current=std::move(checked);return true;
    }
    BillingStatus verify(const BillingChunk &chunk){
        if(!boundedChunk(chunk))return BillingStatus::Invalid;
        if(!sameEvidence(binding.evidence,chunk.trace.evidence))return BillingStatus::Attribution;
        const auto body=chunkText(chunk);if(body.size()>chunkLimit)return BillingStatus::Capacity;
        Statement q(this,"SELECT body FROM events WHERE sequence=? OR id=?",{static_cast<int64_t>(chunk.sequence),chunk.id});std::string original;
        const auto code=q.step();if(code==101)return BillingStatus::SequenceGap;
        if(code!=100||!text(q.value,0,original,chunkLimit))return fail();
        if(original!=body)return BillingStatus::Conflict;
        const auto end=q.step();if(end!=101)return end==100?BillingStatus::Conflict:fail();
        return BillingStatus::Replay;
    }
};
const char *billingStatusName(BillingStatus status){
    switch(status){case BillingStatus::Ok:return "fixture accrual prefix available";case BillingStatus::Replay:return "exact committed chunk replay";case BillingStatus::Invalid:return "invalid billing input";
        case BillingStatus::Unsupported:return "unsupported source or operation";case BillingStatus::Attribution:return "immutable billing binding mismatch";case BillingStatus::Conflict:return "conflicting billing identity/input";
        case BillingStatus::SequenceGap:return "missing billing chunk";case BillingStatus::Clock:return "invalid billing observation clock";case BillingStatus::Capacity:return "billing capacity exhausted";
        case BillingStatus::Calculation:return "incomplete/unsupported rate, energy or settlement context";case BillingStatus::Storage:return "billing storage unavailable; preserve originals";}
    return "unknown billing state";
}
BillingLedger::BillingLedger():impl_(new Impl){}
BillingLedger::~BillingLedger()=default;
BillingStatus BillingLedger::open(const std::filesystem::path &path,const BillingBinding &binding,int64_t now,BillingOpen mode,BillingLimits limits){
    using S=BillingStatus;auto &s=*impl_;if(s.attempted)return S::Invalid;s.attempted=true;
    if(binding.evidence.origin!=PowerOrigin::SyntheticFixture||binding.plan.origin!=TariffOrigin::SyntheticFixture)return S::Unsupported;
    if(!key(binding.cycleKey)||binding.cycleKey.rfind("fixture.",0)!=0||!key(binding.evidence.scope)||!key(binding.evidence.adapter)||!boundedPlan(binding.plan)||!validLimits(limits)||
        !instant(now)||!instant(binding.cycleBeginUtcMs)||binding.cycleBeginUtcMs>now||!path.is_absolute()||(mode!=BillingOpen::Existing&&mode!=BillingOpen::CreateNew&&mode!=BillingOpen::Inspect))return S::Invalid;
    if(validateTariffOpening(binding.plan,initial(binding))!=ElectricityStatus::Ok)return S::Calculation;
    s.binding=binding;s.limits=limits;s.readOnly=mode==BillingOpen::Inspect;s.bound=bindingText(binding,limits);if(s.bound.size()>bindingLimit)return S::Capacity;
    try{
        if(!s.api.load())return S::Storage;
        std::error_code ec;
        if(mode==BillingOpen::CreateNew)std::filesystem::create_directories(path.parent_path(),ec);
        else if(!std::filesystem::is_regular_file(std::filesystem::symlink_status(path,ec))||ec)return S::Storage;
        if(ec)return S::Storage;
        auto lock=path;lock+=".lock";if(!s.lease.acquire(lock))return S::Storage;
        const auto status=std::filesystem::symlink_status(path,ec);const bool absent=ec==std::errc::no_such_file_or_directory||(!ec&&status.type()==std::filesystem::file_type::not_found);
        if(mode==BillingOpen::CreateNew){if(!absent)return S::Conflict;
            for(const auto *suffix:{"-wal","-shm","-journal"}){
                auto sidecar=path;sidecar+=suffix;const auto st=std::filesystem::symlink_status(sidecar,ec);
                if(ec!=std::errc::no_such_file_or_directory&&(ec||st.type()!=std::filesystem::file_type::not_found))return S::Storage;
            }
#if defined(_WIN32)
            HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return S::Storage;CloseHandle(f);
#else
            const int fd=::open(path.c_str(),O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return S::Storage;::close(fd);
#endif
        }else if(absent||ec||!std::filesystem::is_regular_file(status)||std::filesystem::file_size(path)>limits.maxDatabaseBytes)return S::Storage;
        if(s.api.openV2(path.u8string().c_str(),&s.db,(s.readOnly?1:2)|0x10000|0x1000000,nullptr)!=0)return S::Storage;
        s.api.busyTimeout(s.db,0);s.api.limit(s.db,0,static_cast<int>(bindingLimit+4096));s.api.limit(s.db,1,65536);s.api.limit(s.db,2,32);s.api.limit(s.db,3,32);s.api.limit(s.db,7,0);
        int64_t app=0,version=0;if(!s.scalar("PRAGMA application_id",app)||!s.scalar("PRAGMA user_version",version)||(!absent&&(app!=application||version!=1)))return S::Storage;
        if(!s.exec("PRAGMA trusted_schema=OFF; PRAGMA foreign_keys=ON; PRAGMA temp_store=MEMORY; PRAGMA cache_size=-2048;"))return S::Storage;
        if(!absent){int64_t extra=0;
            if(!s.isText("PRAGMA quick_check(1)","ok")||!s.scalar("SELECT count(*) FROM sqlite_master WHERE type IN ('trigger','view') OR (type='table' AND name NOT IN ('meta','events'))",extra)||extra||
                !s.isText("SELECT sql FROM sqlite_master WHERE type='table' AND name='meta'",metaSql)||!s.isText("SELECT sql FROM sqlite_master WHERE type='table' AND name='events'",eventsSql))return S::Storage;
            if(!s.isText("SELECT binding FROM meta WHERE id=1",s.bound))return S::Attribution;
            if(!s.audit())return S::Storage;
            if(now<s.current.observedUtcMs)return S::Clock;
        }
        if(s.readOnly){if(!s.isText("PRAGMA journal_mode","wal"))return S::Storage;}
        else if(!s.isText("PRAGMA journal_mode=WAL","wal")||!s.exec("PRAGMA synchronous=FULL; PRAGMA wal_autocheckpoint=256; PRAGMA journal_size_limit=1048576;"))return S::Storage;
        int64_t page=0,pages=0;if(!s.scalar("PRAGMA page_size",page)||page<512||page>65536)return S::Storage;
        if(!s.scalar("PRAGMA page_count",pages)||pages<0||(!absent&&pages<1)||static_cast<uint64_t>(pages)>limits.maxDatabaseBytes/static_cast<uint64_t>(page))return S::Storage;
        if(!s.readOnly&&!s.exec("PRAGMA max_page_count="+n(static_cast<int64_t>(limits.maxDatabaseBytes/static_cast<uint64_t>(page)))))return S::Storage;
        if(absent){s.current.status=S::Ok;s.current.opening=initial(binding);s.current.digest=sha256Hex(s.bound);
            if(!s.exec("BEGIN IMMEDIATE; "+metaSql+"; "+eventsSql+";")||!s.write("INSERT INTO meta VALUES(1,?,0,?,?)",{s.bound,s.current.digest,stateText(s.current)})||
                !s.exec("PRAGMA application_id="+n(application)+"; PRAGMA user_version=1; COMMIT;")){s.exec("ROLLBACK;");return S::Storage;}}
        s.ready=true;return S::Ok;
    }catch(...){return s.fail();}
}
BillingStatus BillingLedger::apply(const BillingChunk &chunk,int64_t now){
    using S=BillingStatus;auto &s=*impl_;if(!s.ready)return S::Storage;if(s.readOnly)return S::Unsupported;
    if(!instant(now)||now<s.current.observedUtcMs||chunk.observedUtcMs>now)return S::Clock;
    if(!boundedChunk(chunk))return S::Invalid;
    try{
        const auto replay=s.verify(chunk);if(replay!=S::SequenceGap)return replay;
        auto next=s.current;const auto status=advance(s.binding,s.limits,chunk,next);if(status!=S::Ok)return status;
        const auto body=chunkText(chunk),state=stateText(next);if(body.size()>chunkLimit)return S::Capacity;
        next.digest=sha256Hex(s.current.digest+"|"+body+"|"+state);
        if(!s.exec("BEGIN IMMEDIATE;"))return s.fail(s.lastCode);
        if(!s.write("INSERT INTO events VALUES(?,?,?,?,?)",{static_cast<int64_t>(chunk.sequence),chunk.id,body,next.digest,state})||
            !s.write("UPDATE meta SET sequence=?,digest=?,state=? WHERE id=1",{static_cast<int64_t>(chunk.sequence),next.digest,state})){const auto code=s.lastCode;s.exec("ROLLBACK;");return s.fail(code);}
#ifdef CANARY_BILLING_TESTING
        if(s.crash)std::_Exit(92);
#endif
        if(!s.exec("COMMIT;")){const auto code=s.lastCode;s.exec("ROLLBACK;");return s.fail(code);}
        s.current=std::move(next);return S::Ok;
    }catch(...){s.exec("ROLLBACK;");return s.fail();}
}
BillingStatus BillingLedger::verifyCommitted(const BillingChunk &chunk,int64_t now)const{
    auto &s=*impl_;if(!s.ready)return BillingStatus::Storage;
    if(!instant(now)||now<s.current.observedUtcMs||chunk.observedUtcMs>now)return BillingStatus::Clock;
    try{return s.verify(chunk);}catch(...){return s.fail();}
}
BillingSnapshot BillingLedger::snapshot(int64_t now)const{
    const auto &s=*impl_;if(!s.ready)return {};
    if(!instant(now)||now<s.current.observedUtcMs||now<s.binding.cycleBeginUtcMs){BillingSnapshot result;result.status=BillingStatus::Clock;return result;}
    return s.current;
}
CounterfactualBill BillingLedger::projectRemainder(const PowerTrace &base,const PowerTrace &with,int64_t decision)const{
    const auto &s=*impl_;CounterfactualBill result;
    if(!s.ready||!s.current.sequence)return result;
    if(!instant(decision)||decision<s.current.observedUtcMs){result.status=ElectricityStatus::ClockFault;return result;}
    if(s.current.settledTotal){result.status=ElectricityStatus::Unsupported;return result;}
    return counterfactualBill(s.binding.plan,s.current.opening,base,with,s.binding.cycleEndUtcMs,decision);
}
#ifdef CANARY_BILLING_TESTING
void BillingLedgerTestPeer::crashBeforeCommit(BillingLedger &ledger){ledger.impl_->crash=true;}
#endif
} // namespace om
