#include "tariff_declaration.h"
#include <algorithm>
#include <initializer_list>
#include <set>

namespace om {
namespace {
constexpr uint64_t lastMs=253402300799999ULL;
struct Invalid {TariffProblem problem;};
[[noreturn]] void bad(const std::string &path,const std::string &message){throw Invalid{{path,message}};}
void keys(const JsonValue &v,const std::string &path,std::initializer_list<const char *> names){
    if(!v.isObject()||v.members.size()!=names.size())bad(path,"Missing/unknown fields; unsupported rules are never discarded.");
    for(const auto *name:names)if(!v.find(name))bad(path,"Missing/unknown fields; reopen the documented schema.");
    std::set<std::string> seen;for(const auto &m:v.members)if(!seen.insert(m.first).second)bad(path,"Duplicate field.");
}
std::string label(const JsonValue &v,const std::string &path,bool empty=false){
    if(!v.isString()||v.text.size()>128||(!empty&&v.text.empty())||
       (!v.text.empty()&&(v.text.front()==' '||v.text.back()==' '))||
       std::any_of(v.text.begin(),v.text.end(),[](unsigned char c){return c<32||c>126;}))bad(path,"Use 1..128 printable ASCII characters, no surrounding spaces or credentials.");
    return v.text;
}
uint64_t integer(const JsonValue &v,const std::string &path,uint64_t max=lastMs){
    uint64_t n=0;if(!v.asUint64(&n)||n>max)bad(path,"Expected a bounded nonnegative integer in the stated units.");return n;
}
ExactDecimal decimal(const JsonValue &v,const std::string &path){
    ExactDecimal d;
    if(!v.isString()||!ExactDecimal::parse(v.text,&d)||d.text()!=v.text||d.scale()>18||d.digits()>38)
        bad(path,"Enter a canonical decimal string, <=38 digits / 18 decimals. Empty is missing; explicit zero is valid.");
    return d;
}
std::string choice(const JsonValue &v,const std::string &path,std::initializer_list<const char *> allowed){
    const auto value=label(v,path);for(const auto *s:allowed)if(value==s)return value;
    std::string message="Choose ";for(const auto *s:allowed){if(message!="Choose ")message+=" / ";message+=s;}bad(path,message+".");
}
void boundedArray(const JsonValue &v,const std::string &path,size_t max,size_t &total){
    if(!v.isArray()||v.items.size()>max||(total+=v.items.size())>512)bad(path,"Collection bound exceeded (16 revisions, 32 calendar/bands, 256 dynamic, 512 total rows).");
}
bool null(const JsonValue &v){return v.type==JsonValue::Type::Null;}
JsonValue literal(const std::string &s){JsonValue v;parseJsonStrict(s,&v,nullptr);return v;}
std::string child(const std::string &path,const std::string &name){return path.empty()?name:path+"."+name;}
void changes(const JsonValue &a,const JsonValue &b,const std::string &path,std::vector<std::string> &out){
    if(tariffJson(a)==tariffJson(b))return;
    if(a.isObject()&&b.isObject()){
        for(const auto &m:b.members){const auto *old=a.find(m.first);changes(old?*old:JsonValue{},m.second,child(path,m.first),out);}return;
    }
    if(a.isArray()||b.isArray()){
        if(a.items.size()!=b.items.size())out.push_back(path+" rows: "+std::to_string(a.items.size())+" -> "+std::to_string(b.items.size()));
        for(size_t i=0;i<(std::max)(a.items.size(),b.items.size());++i)
            changes(i<a.items.size()?a.items[i]:JsonValue{},i<b.items.size()?b.items[i]:JsonValue{},path+"["+std::to_string(i)+"]",out);
        return;
    }
    if(b.isObject()){for(const auto &m:b.members)changes({},m.second,child(path,m.first),out);return;}
    if(a.isObject()){for(const auto &m:a.members)changes(m.second,{},child(path,m.first),out);return;}
    out.push_back(path+": "+tariffJson(a)+" -> "+tariffJson(b));
}
} // namespace

std::string tariffJson(const JsonValue &v){
    if(v.isString())return jsonQuote(v.text);
    if(v.isNumber())return v.text;
    if(v.isBool())return v.boolean?"true":"false";
    if(v.isArray()){std::string s="[";for(const auto &i:v.items){if(s!="[")s+=",";s+=tariffJson(i);}return s+"]";}
    if(v.isObject()){std::string s="{";for(const auto &m:v.members){if(s!="{")s+=",";s+=jsonQuote(m.first)+":"+tariffJson(m.second);}return s+"}";}
    return "null";
}
JsonValue tariffRowTemplate(const std::string &name){
    if(name=="revisions")return literal(R"({"key":"","begin_utc_ms":0,"end_utc_ms":0,"published_utc_ms":0,"time_mode":"constant","base_rate_per_kwh":"","charge_per_civil_day":"","calendar":[],"dynamic":[],"bands":[]})");
    if(name=="calendar")return literal(R"({"key":"","begin_minute":0,"end_minute":1440,"weekday_mask":127,"month_mask":4095,"priority":0,"date":null,"published_utc_ms":0,"rate_per_kwh":""})");
    if(name=="dynamic")return literal(R"({"begin_utc_ms":0,"end_utc_ms":0,"published_utc_ms":0,"rate_per_kwh":""})");
    if(name=="bands")return literal(R"({"through_kwh":null,"rate_per_kwh":""})");
    return {};
}
JsonValue tariffDeclarationTemplate(){
    auto v=literal(R"({"schema_version":1,"country":"","source_reference":"","acknowledged":false,"confirmed_utc":0,"reset_policy":"explicit_cycle_only","plan":{"key":"","territory":"","supplier":"","product":"","currency":"","clock":{"kind":"utc","fixed_offset_seconds":0},"tiers":"none","rounding":"final_display_only","cycle_charge":"","minimum_cycle_subtotal":null,"subtotal_multiplier":"","revisions":[]},"opening":{"key":"","scope":"","adapter":"user-declared-offline","basis":"whole_meter_import","origin":"declared_forecast","cycle_begin_utc_ms":0,"cycle_end_utc_ms":0,"through_utc_ms":0,"imported_kwh":"","accrued_subtotal":"","time_database_version":"explicit-fixed-clock"}})");
    for(auto &m:v.members)if(m.first=="plan")for(auto &p:m.second.members)if(p.first=="revisions")p.second.items.push_back(tariffRowTemplate("revisions"));
    return v;
}
bool decodeTariffDeclaration(const JsonValue &root,TariffDeclaration *out,TariffProblem *problem){
    try{
        if(!out)bad("","No declaration output.");
        if(tariffJson(root).size()>tariffDeclarationMaxBytes)bad("","Declaration exceeds 64 KiB.");
        keys(root,"",{"schema_version","country","source_reference","acknowledged","confirmed_utc","reset_policy","plan","opening"});
        if(integer(*root.find("schema_version"),"schema_version",1)!=1)bad("schema_version","Unsupported schema.");
        choice(*root.find("reset_policy"),"reset_policy",{"explicit_cycle_only"});
        const auto &ack=*root.find("acknowledged");if(!ack.isBool()||!ack.boolean)bad("acknowledged","Acknowledge declared-model exclusions before saving.");
        TariffDeclaration d;d.country=label(*root.find("country"),"country");d.sourceReference=label(*root.find("source_reference"),"source_reference");
        d.confirmedUtc=integer(*root.find("confirmed_utc"),"confirmed_utc",lastMs/1000);if(!d.confirmedUtc)bad("confirmed_utc","Explicit review confirmation required.");
        auto &p=d.plan;const auto &plan=*root.find("plan");
        keys(plan,"plan",{"key","territory","supplier","product","currency","clock","tiers","rounding","cycle_charge","minimum_cycle_subtotal","subtotal_multiplier","revisions"});
        p.declared=true;p.origin=TariffOrigin::UserDeclaredOffline;p.rounding=TariffRounding::FinalDisplayOnly;
        choice(*plan.find("rounding"),"plan.rounding",{"final_display_only"});
        for(const auto &binding:std::vector<std::pair<const char *,std::string *>>{{"key",&p.key},{"territory",&p.territory},{"supplier",&p.supplier},{"product",&p.product},{"currency",&p.currency}})
            *binding.second=label(*plan.find(binding.first),child("plan",binding.first));
        if(p.currency.size()!=3||p.currency.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ")!=std::string::npos)bad("plan.currency","Three uppercase letters required; no currency/FX inference.");
        const auto &clock=*plan.find("clock");keys(clock,"plan.clock",{"kind","fixed_offset_seconds"});
        const auto kind=choice(*clock.find("kind"),"plan.clock.kind",{"utc","fixed_standard"});
        const auto &offset=*clock.find("fixed_offset_seconds");auto abs=offset;const bool negative=!offset.text.empty()&&offset.text.front()=='-';if(negative)abs.text.erase(0,1);
        const auto magnitude=integer(abs,"plan.clock.fixed_offset_seconds",64800);
        if(magnitude%60||(kind=="utc"&&magnitude)||offset.text=="-0")bad("plan.clock.fixed_offset_seconds","Whole-minute STANDARD offset within +/-18h; UTC requires zero.");
        p.clock.kind=kind=="utc"?CivilClockKind::Utc:CivilClockKind::FixedStandardOffset;p.clock.fixedOffsetSeconds=static_cast<int32_t>(magnitude)*(negative?-1:1);
        const auto tiers=choice(*plan.find("tiers"),"plan.tiers",{"none","progressive","all_units"});p.tiers=tiers=="none"?TierMode::None:tiers=="progressive"?TierMode::Progressive:TierMode::AllUnits;
        p.cycleCharge=decimal(*plan.find("cycle_charge"),"plan.cycle_charge");
        if(!null(*plan.find("minimum_cycle_subtotal")))p.minimumCycleSubtotal=decimal(*plan.find("minimum_cycle_subtotal"),"plan.minimum_cycle_subtotal");
        p.subtotalMultiplier=decimal(*plan.find("subtotal_multiplier"),"plan.subtotal_multiplier");
        size_t total=0,index=0;
        boundedArray(*plan.find("revisions"),"plan.revisions",16,total);
        for(const auto &row:plan.find("revisions")->items){
            const auto at="plan.revisions["+std::to_string(index++)+"]";TariffRevision r;
            keys(row,at,{"key","begin_utc_ms","end_utc_ms","published_utc_ms","time_mode","base_rate_per_kwh","charge_per_civil_day","calendar","dynamic","bands"});
            r.key=label(*row.find("key"),child(at,"key"));r.beginUtcMs=integer(*row.find("begin_utc_ms"),child(at,"begin_utc_ms"));r.endUtcMs=integer(*row.find("end_utc_ms"),child(at,"end_utc_ms"));r.publishedUtcMs=integer(*row.find("published_utc_ms"),child(at,"published_utc_ms"));
            const auto mode=choice(*row.find("time_mode"),child(at,"time_mode"),{"constant","calendar","dynamic"});r.timeMode=mode=="constant"?TimeRateMode::Constant:mode=="calendar"?TimeRateMode::Calendar:TimeRateMode::Dynamic;
            r.baseRatePerKwh=decimal(*row.find("base_rate_per_kwh"),child(at,"base_rate_per_kwh"));r.chargePerCivilDay=decimal(*row.find("charge_per_civil_day"),child(at,"charge_per_civil_day"));
            size_t n=0;
            boundedArray(*row.find("calendar"),child(at,"calendar"),32,total);
            for(const auto &c:row.find("calendar")->items){
                const auto path=at+".calendar["+std::to_string(n++)+"]";CalendarRate rate;
                keys(c,path,{"key","begin_minute","end_minute","weekday_mask","month_mask","priority","date","published_utc_ms","rate_per_kwh"});
                rate.key=label(*c.find("key"),child(path,"key"));rate.beginMinute=static_cast<unsigned>(integer(*c.find("begin_minute"),child(path,"begin_minute"),1440));rate.endMinute=static_cast<unsigned>(integer(*c.find("end_minute"),child(path,"end_minute"),1440));
                rate.weekdayMask=static_cast<unsigned>(integer(*c.find("weekday_mask"),child(path,"weekday_mask"),127));rate.monthMask=static_cast<unsigned>(integer(*c.find("month_mask"),child(path,"month_mask"),4095));rate.priority=static_cast<unsigned>(integer(*c.find("priority"),child(path,"priority"),1000));
                const auto &date=*c.find("date");if(!null(date)){
                    const auto s=label(date,child(path,"date"));
                    if(s.size()!=10||s[4]!='-'||s[7]!='-'||s.substr(0,4).find_first_not_of("0123456789")!=std::string::npos||s.substr(5,2).find_first_not_of("0123456789")!=std::string::npos||s.substr(8).find_first_not_of("0123456789")!=std::string::npos)bad(child(path,"date"),"Use YYYY-MM-DD or clear for no exact-date condition.");
                    rate.date=CivilDate{std::stoi(s.substr(0,4)),static_cast<unsigned>(std::stoi(s.substr(5,2))),static_cast<unsigned>(std::stoi(s.substr(8,2)))};
                }
                rate.publishedUtcMs=integer(*c.find("published_utc_ms"),child(path,"published_utc_ms"));rate.ratePerKwh=decimal(*c.find("rate_per_kwh"),child(path,"rate_per_kwh"));r.calendar.push_back(std::move(rate));
            }
            boundedArray(*row.find("dynamic"),child(at,"dynamic"),256,total);
            n=0;for(const auto &v:row.find("dynamic")->items){
                const auto path=at+".dynamic["+std::to_string(n++)+"]";keys(v,path,{"begin_utc_ms","end_utc_ms","published_utc_ms","rate_per_kwh"});
                r.dynamic.push_back({static_cast<int64_t>(integer(*v.find("begin_utc_ms"),child(path,"begin_utc_ms"))),static_cast<int64_t>(integer(*v.find("end_utc_ms"),child(path,"end_utc_ms"))),static_cast<int64_t>(integer(*v.find("published_utc_ms"),child(path,"published_utc_ms"))),decimal(*v.find("rate_per_kwh"),child(path,"rate_per_kwh"))});
            }
            boundedArray(*row.find("bands"),child(at,"bands"),32,total);
            n=0;for(const auto &v:row.find("bands")->items){
                const auto path=at+".bands["+std::to_string(n++)+"]";keys(v,path,{"through_kwh","rate_per_kwh"});TariffBand band;
                if(!null(*v.find("through_kwh")))band.throughKwh=decimal(*v.find("through_kwh"),child(path,"through_kwh"));
                band.ratePerKwh=decimal(*v.find("rate_per_kwh"),child(path,"rate_per_kwh"));r.bands.push_back(std::move(band));
            }
            p.revisions.push_back(std::move(r));
        }
        auto &s=d.opening;const auto &opening=*root.find("opening");
        keys(opening,"opening",{"key","scope","adapter","basis","origin","cycle_begin_utc_ms","cycle_end_utc_ms","through_utc_ms","imported_kwh","accrued_subtotal","time_database_version"});
        choice(*opening.find("basis"),"opening.basis",{"whole_meter_import"});choice(*opening.find("origin"),"opening.origin",{"declared_forecast"});
        s.declared=true;s.planKey=p.key;s.evidence={PowerBasis::WholeMeterImport,PowerOrigin::DeclaredForecast,label(*opening.find("scope"),"opening.scope"),label(*opening.find("adapter"),"opening.adapter")};
        d.openingKey=label(*opening.find("key"),"opening.key");s.timeDatabaseVersion=choice(*opening.find("time_database_version"),"opening.time_database_version",{"explicit-fixed-clock"});
        s.cycleBeginUtcMs=integer(*opening.find("cycle_begin_utc_ms"),"opening.cycle_begin_utc_ms");s.cycleEndUtcMs=integer(*opening.find("cycle_end_utc_ms"),"opening.cycle_end_utc_ms");s.throughUtcMs=integer(*opening.find("through_utc_ms"),"opening.through_utc_ms");
        auto quantity=electricityKwh(decimal(*opening.find("imported_kwh"),"opening.imported_kwh"));if(!quantity)bad("opening.imported_kwh","Nonnegative whole-meter quantity required.");s.importedKwh=*quantity;
        ExactDecimal denominator;ExactDecimal::parse("3600000000",&denominator);
        if(!decimal(*opening.find("accrued_subtotal"),"opening.accrued_subtotal").multiply(denominator,&s.accruedSubtotal.numerator))bad("opening.accrued_subtotal","Arithmetic capacity exceeded.");
        const auto status=validateTariffOpening(p,s);if(status!=ElectricityStatus::Ok)bad("plan/opening",electricityStatusName(status));
        // This store is an offline declaration, so publication is a local claim bounded by confirmation, not certification.
        for(const auto &r:p.revisions){
            if(r.publishedUtcMs>static_cast<int64_t>(d.confirmedUtc*1000))bad("plan.revisions","Publication cannot be after local confirmation.");
            for(const auto &c:r.calendar)if(c.publishedUtcMs>static_cast<int64_t>(d.confirmedUtc*1000))bad("plan.revisions.calendar","Publication cannot be after local confirmation.");
            for(const auto &v:r.dynamic)if(v.publishedUtcMs>static_cast<int64_t>(d.confirmedUtc*1000))bad("plan.revisions.dynamic","Publication cannot be after local confirmation.");
        }
        *out=std::move(d);if(problem)*problem={};return true;
    }catch(const Invalid &e){if(problem)*problem=e.problem;return false;}
}
bool parseTariffDeclaration(const std::string &text,JsonValue *root,TariffProblem *problem){
    JsonValue v;TariffDeclaration d;
    if(!root||text.size()>tariffDeclarationMaxBytes||!parseJsonStrict(text,&v,nullptr)){if(problem)*problem={"","Invalid/oversized JSON; file preserved."};return false;}
    if(!decodeTariffDeclaration(v,&d,problem))return false;
    *root=std::move(v);return true;
}
std::vector<std::string> tariffDeclarationChanges(const JsonValue &a,const JsonValue &b){std::vector<std::string> result;changes(a,b,"",result);return result;}
} // namespace om
