#include "billing_model.h"
#include "json_dom.h"
#include "energy_history_model.h"
#include "hash.h"
namespace om { namespace billing_detail {
namespace {
bool rows(const JsonValue &v,size_t cap){return v.isArray()&&v.items.size()<=cap;}
bool fields(const JsonValue &v,size_t count){return v.isArray()&&v.items.size()==count;}
template<class T>bool number(const JsonValue &v,T &out,uint64_t cap){uint64_t n=0;if(!v.asUint64(&n)||n>cap)return false;out=static_cast<T>(n);return true;}
bool flag(const JsonValue &v,bool &out){return number(v,out,1);}
bool time(const JsonValue &v,int64_t &out){return number(v,out,lastUtc);}
bool text(const JsonValue &v,std::string &out,size_t cap=128){if(!v.isString()||v.text.size()>cap||v.text.find('\0')!=std::string::npos)return false;out=v.text;return true;}
bool decimal(const JsonValue &v,ExactDecimal &out){return v.isString()&&ExactDecimal::parse(v.text,&out);}
bool optional(const JsonValue &v,std::optional<ExactDecimal> &out){if(v.type==JsonValue::Type::Null){out.reset();return true;}ExactDecimal d;if(!decimal(v,d))return false;out=d;return true;}
bool amount(const JsonValue &v,std::optional<ElectricityAmount> &out){std::optional<ExactDecimal> d;if(!optional(v,d))return false;out=d?std::optional<ElectricityAmount>{{*d}}:std::nullopt;return true;}
bool hash(const std::string &s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool evidence(const JsonValue &v,PowerEvidence &e){return fields(v,4)&&number(v.items[0],e.basis,4)&&number(v.items[1],e.origin,3)&&text(v.items[2],e.scope)&&text(v.items[3],e.adapter);}
bool clock(const JsonValue &v,CivilClock &out){
    if(!fields(v,3)||!number(v.items[0],out.kind,2)||!text(v.items[1],out.zone))return false;
    auto offset=v.items[2];const bool negative=!offset.text.empty()&&offset.text[0]=='-';if(negative)offset.text.erase(0,1);
    uint64_t n=0;if(!number(offset,n,64800))return false;out.fixedOffsetSeconds=static_cast<int32_t>(n)*(negative?-1:1);return true;
}
bool date(const JsonValue &v,std::optional<CivilDate> &out){
    if(v.type==JsonValue::Type::Null)return true;
    CivilDate d;if(!fields(v,3)||!number(v.items[0],d.year,9999)||!number(v.items[1],d.month,12)||!number(v.items[2],d.day,31)||!validCivilDate(d))return false;out=d;return true;
}
bool plan(const JsonValue &v,TariffPlan &p){
    if(!fields(v,15))return false;
    const auto &a=v.items;
    if(!flag(a[0],p.declared)||!number(a[1],p.origin,2)||!text(a[2],p.key)||!text(a[3],p.territory)||!text(a[4],p.supplier)||!text(a[5],p.product)||!text(a[6],p.currency,3)||
        !clock(a[7],p.clock)||!number(a[8],p.tiers,2)||!number(a[9],p.rounding,1)||!decimal(a[10],p.cycleCharge)||!optional(a[11],p.minimumCycleSubtotal)||!optional(a[12],p.subtotalMultiplier)||
        !number(a[13],p.unsupportedRules,UINT32_MAX)||!rows(a[14],128))return false;
    size_t total=0;
    for(const auto &entry:a[14].items){if(!fields(entry,10))return false;const auto &r=entry.items;TariffRevision revision;
        if(!text(r[0],revision.key)||!time(r[1],revision.beginUtcMs)||!time(r[2],revision.endUtcMs)||!time(r[3],revision.publishedUtcMs)||!number(r[4],revision.timeMode,2)||
            !decimal(r[5],revision.baseRatePerKwh)||!decimal(r[6],revision.chargePerCivilDay)||!rows(r[7],64)||!rows(r[8],4096)||!rows(r[9],64))return false;
        total+=r[7].items.size()+r[8].items.size()+r[9].items.size();if(total>4096)return false;
        for(const auto &row:r[7].items){if(!fields(row,9))return false;const auto &c=row.items;CalendarRate rate;
            if(!text(c[0],rate.key)||!number(c[1],rate.beginMinute,1440)||!number(c[2],rate.endMinute,1440)||!number(c[3],rate.weekdayMask,127)||!number(c[4],rate.monthMask,4095)||
                !number(c[5],rate.priority,1000)||!date(c[6],rate.date)||!time(c[7],rate.publishedUtcMs)||!decimal(c[8],rate.ratePerKwh))return false;
            revision.calendar.push_back(std::move(rate));}
        for(const auto &row:r[8].items){if(!fields(row,4))return false;const auto &c=row.items;DynamicRate rate;
            if(!time(c[0],rate.beginUtcMs)||!time(c[1],rate.endUtcMs)||!time(c[2],rate.publishedUtcMs)||!decimal(c[3],rate.ratePerKwh))return false;
            revision.dynamic.push_back(std::move(rate));}
        for(const auto &row:r[9].items){TariffBand band;if(!fields(row,2)||!optional(row.items[0],band.throughKwh)||!decimal(row.items[1],band.ratePerKwh))return false;revision.bands.push_back(std::move(band));}
        p.revisions.push_back(std::move(revision));
    }
    return true;
}
}
bool parseBinding(const std::string &wire,BillingBinding &out,BillingLimits &limits){
    JsonValue root;if(wire.size()>bindingLimit||!parseJsonStrict(wire,&root,nullptr)||!root.isArray()||(root.items.size()!=7&&root.items.size()!=8))return false;
    const auto &a=root.items;BillingBinding b;BillingLimits l;
    if(!a[0].isString()||a[0].text!=(a.size()==8?"CMBL2":"CMBL1")||!text(a[1],b.cycleKey)||!time(a[2],b.cycleBeginUtcMs)||!time(a[3],b.cycleEndUtcMs)||!evidence(a[4],b.evidence)||!plan(a[5],b.plan)||
        !fields(a[6],3)||!number(a[6].items[0],l.maxBatches,4096)||!number(a[6].items[1],l.maxIntervals,131072)||!number(a[6].items[2],l.maxDatabaseBytes,128*1024*1024))return false;
    if(a.size()==8){EnergyHistoryRequest source;if(!a[7].isString()||!decodeEnergyHistoryRequest(a[7].text,&source))return false;b.source=BillingEnergySource{source.file,source.binding,source.limits};}
    if(bindingText(b,l)!=wire)return false;
    out=std::move(b);limits=l;return true;
}
bool parseState(const std::string &wire,const BillingBinding &b,const BillingLimits &limits,int64_t now,BillingSnapshot &out){
    JsonValue root;if(wire.size()>2048||!instant(now)||!parseJsonStrict(wire,&root,nullptr)||!fields(root,b.source?12:11))return false;
    const auto &a=root.items;BillingSnapshot s;s.status=BillingStatus::Ok;auto &o=s.opening;o.declared=true;o.planKey=b.plan.key;o.evidence=b.evidence;o.cycleBeginUtcMs=b.cycleBeginUtcMs;o.cycleEndUtcMs=b.cycleEndUtcMs;
    if(!number(a[0],s.sequence,limits.maxBatches)||!number(a[1],s.intervals,limits.maxIntervals)||!text(a[2],s.batchId)||!time(a[3],s.observedUtcMs)||!time(a[4],o.throughUtcMs)||!text(a[5],o.timeDatabaseVersion)||
        !decimal(a[6],o.importedKwh.numerator)||o.importedKwh.numerator.negative()||!decimal(a[7],o.accruedSubtotal.numerator)||!amount(a[8],s.provisionalTotal)||!amount(a[9],s.settledTotal)||!flag(a[10],s.settlementDependent))return false;
    if(b.source){BillingSnapshot::SourceProgress p;if(!fields(a[11],3)||!number(a[11].items[0],p.checkpoint.sequence,b.source->limits.compaction?energy_history_detail::lifetimeBatchLimit:b.source->limits.maxBatches)||
            !text(a[11].items[1],p.checkpoint.digest,64)||!hash(p.checkpoint.digest)||!time(a[11].items[2],p.queriedUtcMs))return false;
        s.source=std::move(p);}
    if(!s.sequence){
        if(s.intervals||!s.batchId.empty()||s.observedUtcMs||o.throughUtcMs!=b.cycleBeginUtcMs||!o.timeDatabaseVersion.empty()||!o.importedKwh.numerator.zero()||!o.accruedSubtotal.numerator.zero()||s.provisionalTotal||s.settledTotal||s.settlementDependent||
            (s.source&&(s.source->checkpoint.sequence||s.source->queriedUtcMs||s.source->checkpoint.digest!=sha256Hex(energy_history_detail::bindingText(b.source->binding)))))return false;
    }else{
        if(s.intervals<s.sequence||s.batchId.empty()||o.throughUtcMs<=b.cycleBeginUtcMs||o.throughUtcMs>b.cycleEndUtcMs||s.observedUtcMs<o.throughUtcMs||s.observedUtcMs>now||!s.provisionalTotal||
            s.settledTotal.has_value()!=(o.throughUtcMs==b.cycleEndUtcMs)||o.timeDatabaseVersion!=civilAt(b.plan.clock,b.cycleBeginUtcMs).databaseVersion||
            (s.settledTotal&&s.provisionalTotal->numerator.compare(s.settledTotal->numerator)!=0)||s.settlementDependent!=(!s.settledTotal&&(b.plan.tiers==TierMode::AllUnits||b.plan.minimumCycleSubtotal.has_value()))||
            (s.source&&(!s.source->checkpoint.sequence||s.source->queriedUtcMs<s.observedUtcMs||s.source->queriedUtcMs>now)))return false;
    }
    if(stateText(s)!=wire)return false;
    out=std::move(s);return true;
}
} } // namespace om::billing_detail
