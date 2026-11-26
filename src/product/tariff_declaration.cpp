#include "tariff_declaration.h"
#include <algorithm>
#include <initializer_list>
#include <set>
#include <tuple>

namespace om {
namespace {
constexpr uint64_t lastMs=253402300799999ULL;
struct Invalid {TariffProblem problem;};
[[noreturn]] void bad(const std::string &path,const std::string &message,TariffProblemKind kind,std::vector<std::string> choices={},std::optional<ElectricityStatus> status={}){throw Invalid{{path,message,kind,std::move(choices),status}};}
void keys(const JsonValue &v,const std::string &path,std::initializer_list<const char *> names,
          std::initializer_list<const char *> additional={},std::initializer_list<const char *> extra={}){
    if(!v.isObject()||v.members.size()!=names.size()+additional.size()+extra.size())bad(path,"Missing/unknown fields; unsupported rules are never discarded.",TariffProblemKind::MissingFields);
    for(const auto fields:{names,additional,extra})for(const auto *name:fields)
        if(!v.find(name))bad(path,"Missing/unknown fields; reopen the documented schema.",TariffProblemKind::SchemaFields);
    std::set<std::string> seen;for(const auto &m:v.members)if(!seen.insert(m.first).second)bad(path,"Duplicate field.",TariffProblemKind::DuplicateField);
}
std::string label(const JsonValue &v,const std::string &path,bool empty=false){
    if(!v.isString()||v.text.size()>128||(!empty&&v.text.empty())||
       (!v.text.empty()&&(v.text.front()==' '||v.text.back()==' '))||
       std::any_of(v.text.begin(),v.text.end(),[](unsigned char c){return c<32||c>126;}))bad(path,"Use 1..128 printable ASCII characters, no surrounding spaces or credentials.",TariffProblemKind::Label);
    return v.text;
}
uint64_t integer(const JsonValue &v,const std::string &path,uint64_t max=lastMs){
    uint64_t n=0;if(!v.asUint64(&n)||n>max)bad(path,"Expected a bounded nonnegative integer in the stated units.",TariffProblemKind::Integer);return n;
}
ExactDecimal decimal(const JsonValue &v,const std::string &path){
    ExactDecimal d;
    if(!v.isString()||!ExactDecimal::parse(v.text,&d)||d.text()!=v.text||d.scale()>18||d.digits()>38)
        bad(path,"Enter a canonical decimal string, <=38 digits / 18 decimals. Empty is missing; explicit zero is valid.",TariffProblemKind::Decimal);
    return d;
}
ExactDecimal componentRate(const ExactDecimal &unscaled,const ExactDecimal &scaled,const JsonValue &factor,
                           const std::string &path,bool noFallback=false){
    const auto multiplier=decimal(factor,path);
    if(multiplier.negative()||(noFallback&&!multiplier.zero()))
        bad(path,noFallback?"Use zero for the unused dynamic fallback.":"Enter a nonnegative multiplier.",TariffProblemKind::Unknown);
    ExactDecimal adjusted;
    if(!scaled.multiply(multiplier,&adjusted)||!unscaled.add(adjusted,&adjusted))
        bad(path,"Arithmetic capacity exceeded.",TariffProblemKind::ArithmeticCapacity);
    return adjusted;
}
std::string choice(const JsonValue &v,const std::string &path,std::initializer_list<const char *> allowed){
    const auto value=label(v,path);for(const auto *s:allowed)if(value==s)return value;
    std::string message="Choose ";std::vector<std::string> choices;for(const auto *s:allowed){if(message!="Choose ")message+=" / ";message+=s;choices.emplace_back(s);}bad(path,message+".",TariffProblemKind::Choice,std::move(choices));
}
void boundedArray(const JsonValue &v,const std::string &path,size_t max,size_t &total){
    if(!v.isArray()||v.items.size()>max||(total+=v.items.size())>512)bad(path,"Collection bound exceeded (16 revisions, 32 calendar/bands, 256 dynamic, 512 total rows).",TariffProblemKind::Collection);
}
bool null(const JsonValue &v){return v.type==JsonValue::Type::Null;}
JsonValue literal(const std::string &s){JsonValue v;parseJsonStrict(s,&v,nullptr);return v;}
std::string child(const std::string &path,const std::string &name){return path.empty()?name:path+"."+name;}
CivilClock decodeClock(const JsonValue &clock,const std::string &path,bool allowNamed){
    const auto *type=clock.find("kind");
    if(allowNamed&&type&&type->isString()&&type->text=="named_iana"){
        keys(clock,path,{"kind","zone","time_database_version"});
        const auto versionPath=child(path,"time_database_version");
        choice(*clock.find("time_database_version"),versionPath,{tariffTzdbVersion});
        CivilClock result{CivilClockKind::NamedIana,label(*clock.find("zone"),child(path,"zone")),0};
        const auto stamp=civilAt(result,86400000);
        if(stamp.status!=CivilStatus::Ok||stamp.databaseVersion!=tariffTzdbVersion)
            bad(child(path,"zone"),"Named zone requires the reviewed tariff time database and a known IANA zone.",TariffProblemKind::Unknown);
        return result;
    }
    keys(clock,path,{"kind","fixed_offset_seconds"});
    const auto kind=choice(*clock.find("kind"),child(path,"kind"),{"utc","fixed_standard"});
    const auto offsetPath=child(path,"fixed_offset_seconds");
    const auto &offset=*clock.find("fixed_offset_seconds");
    auto absolute=offset;
    const bool negative=!offset.text.empty()&&offset.text.front()=='-';
    if(negative)absolute.text.erase(0,1);
    const auto magnitude=integer(absolute,offsetPath,64800);
    if(magnitude%60||(kind=="utc"&&magnitude)||offset.text=="-0")
        bad(offsetPath,"Whole-minute STANDARD offset within +/-18h; UTC requires zero.",TariffProblemKind::FixedOffset);
    CivilClock result;
    result.kind=kind=="utc"?CivilClockKind::Utc:CivilClockKind::FixedStandardOffset;
    result.fixedOffsetSeconds=static_cast<int32_t>(magnitude)*(negative?-1:1);
    return result;
}
CivilDate offsetMonth(CivilDate date,int offset){
    const int month=static_cast<int>(date.month)+offset;
    if(month<1)return {date.year-1,static_cast<unsigned>(month+12),1};
    if(month>12)return {date.year+1,static_cast<unsigned>(month-12),1};
    return {date.year,static_cast<unsigned>(month),1};
}
CivilDate monthlyDate(CivilDate month,unsigned day){
    month.day=1;
    if(!validCivilDate(month))return month;
    month.day=day;
    while(!validCivilDate(month))--month.day;
    return month;
}
TariffResetSchedule decodeResetSchedule(const JsonValue &policy, bool allowNamed, TariffResetCadence cadence) {
    const bool annual = cadence == TariffResetCadence::Annual;
    const bool bimonthly = cadence == TariffResetCadence::Bimonthly;
    const auto *allocation = policy.find("daily_fee_allocation");
    if (bimonthly) {
        keys(policy, "reset_policy", {"bimonthly_start_month", "bimonthly_day", "clock"});
    } else if (annual) {
        if (allocation) keys(policy, "reset_policy", {"annual_month", "annual_day", "clock", "daily_fee_allocation"});
        else keys(policy, "reset_policy", {"annual_month", "annual_day", "clock"});
    } else {
        if (allocation) keys(policy, "reset_policy", {"monthly_day", "clock", "daily_fee_allocation"});
        else keys(policy, "reset_policy", {"monthly_day", "clock"});
    }
    if (allocation) choice(*allocation, "reset_policy.daily_fee_allocation", {"prorate_by_cycle_minutes"});
    const char *dayField = annual ? "annual_day" : bimonthly ? "bimonthly_day" : "monthly_day";
    const auto dayPath = child("reset_policy", dayField);
    const auto day = static_cast<unsigned>(integer(*policy.find(dayField), dayPath, 31));
    if (!day) bad(dayPath, "Enter a reset day from 1 to 31.", TariffProblemKind::Unknown);
    unsigned month = 0;
    if (annual) {
        month = static_cast<unsigned>(integer(*policy.find("annual_month"), "reset_policy.annual_month", 12));
        if (!month) bad("reset_policy.annual_month", "Enter a month from 1 to 12.", TariffProblemKind::Unknown);
    }
    unsigned startMonth = 0;
    if (bimonthly) {
        constexpr char path[] = "reset_policy.bimonthly_start_month";
        startMonth = static_cast<unsigned>(integer(*policy.find("bimonthly_start_month"), path, 2));
        if (!startMonth) bad(path, "Choose 1 for odd months or 2 for even months.", TariffProblemKind::Unknown);
    }
    return {day, decodeClock(*policy.find("clock"), "reset_policy.clock", allowNamed),
            allocation ? DailyFeeAllocation::ProrateByCycleMinutes : DailyFeeAllocation::WholeDay, month, startMonth};
}

unsigned seasonalDateKey(unsigned month,unsigned day) {
    return month * 32 + day;
}

SeasonalAllowance decodeSeasonalAllowance(const JsonValue &value,int64_t confirmedUtcMs) {
    keys(value,"seasonal_allowance",{"published_utc_ms","seasons"});
    SeasonalAllowance result;
    constexpr char publication[]="seasonal_allowance.published_utc_ms";
    result.publishedUtcMs=static_cast<int64_t>(integer(*value.find("published_utc_ms"),publication));
    if(result.publishedUtcMs>confirmedUtcMs)
        bad(publication,"Publication cannot be after local confirmation.",TariffProblemKind::FuturePublication);
    const auto &seasons=*value.find("seasons");
    if(!seasons.isArray()||seasons.items.empty()||seasons.items.size()>16)
        bad("seasonal_allowance.seasons","Provide 1 to 16 seasonal start dates.",TariffProblemKind::Unknown);
    unsigned previous=0;
    for(size_t i=0;i<seasons.items.size();++i){
        const auto path="seasonal_allowance.seasons["+std::to_string(i)+"]";
        const auto &row=seasons.items[i];
        keys(row,path,{"start_month","start_day","kwh_per_day"});
        const auto month=static_cast<unsigned>(integer(*row.find("start_month"),child(path,"start_month"),12));
        const auto day=static_cast<unsigned>(integer(*row.find("start_day"),child(path,"start_day"),31));
        if(!validCivilDate({2023,month,day}))
            bad(child(path,"start_day"),"Use a date that occurs in every year.",TariffProblemKind::Date);
        const auto key=seasonalDateKey(month,day);
        if(key<=previous)bad(path,"List the seasonal start dates in strictly increasing order.",TariffProblemKind::Unknown);
        const auto amountPath=child(path,"kwh_per_day");
        auto amount=decimal(*row.find("kwh_per_day"),amountPath);
        if(amount.negative())bad(amountPath,"Daily allowance must be nonnegative.",TariffProblemKind::OpeningQuantity);
        result.seasons.push_back({month,day,std::move(amount)});
        previous=key;
    }
    return result;
}

DemandChargeRule decodeDemandCharge(const JsonValue &value,int64_t confirmedUtcMs,bool apparent) {
    const auto *ratchet=value.find("ratchet");
    using Fields=std::initializer_list<const char *>;
    if(apparent)
        keys(value,"demand_charge",{"window_minutes","offset_minutes","published_utc_ms","rate_per_kva","power_factor_va_places"},
             ratchet?Fields{"ratchet"}:Fields{});
    else
        keys(value,"demand_charge",{"window_minutes","offset_minutes","published_utc_ms","rate_per_kw"},
             ratchet?Fields{"ratchet"}:Fields{});
    const auto window=static_cast<unsigned>(integer(*value.find("window_minutes"),"demand_charge.window_minutes",60));
    const auto offset=static_cast<unsigned>(integer(*value.find("offset_minutes"),"demand_charge.offset_minutes",59));
    if(window<5||60%window)
        bad("demand_charge.window_minutes","Choose a window from 5 to 60 minutes that divides an hour.",TariffProblemKind::Unknown);
    if(offset>=window)
        bad("demand_charge.offset_minutes","Enter a nonnegative UTC offset smaller than the window.",TariffProblemKind::Unknown);
    const auto published=static_cast<int64_t>(integer(*value.find("published_utc_ms"),"demand_charge.published_utc_ms"));
    if(published>confirmedUtcMs)
        bad("demand_charge.published_utc_ms","Publication cannot be after local confirmation.",TariffProblemKind::FuturePublication);
    const auto *rateField=apparent?"rate_per_kva":"rate_per_kw";
    const auto ratePath=child("demand_charge",rateField);
    auto rate=decimal(*value.find(rateField),ratePath);
    if(rate.negative())bad(ratePath,"Use a nonnegative demand charge in the declared unit.",TariffProblemKind::Unknown);
    DemandChargeRule result{window,offset,published,std::move(rate)};
    result.apparentPower=apparent;
    if(apparent){
        const auto &places=*value.find("power_factor_va_places");
        if(places.type!=JsonValue::Type::Null)
            result.powerFactorVaPlaces=static_cast<unsigned>(integer(places,"demand_charge.power_factor_va_places",18));
    }
    if(ratchet){
        constexpr char path[]="demand_charge.ratchet";
        keys(*ratchet,path,{"lookback_months","fraction"});
        const auto months=static_cast<unsigned>(integer(*ratchet->find("lookback_months"),child(path,"lookback_months"),12));
        auto fraction=decimal(*ratchet->find("fraction"),child(path,"fraction"));
        ExactDecimal one;
        ExactDecimal::parse("1",&one);
        if(!months)bad(child(path,"lookback_months"),"Choose one to twelve completed monthly cycles.",TariffProblemKind::Unknown);
        if(fraction.negative()||fraction.zero()||fraction.compare(one)>0)
            bad(child(path,"fraction"),"Use a positive fraction no larger than one.",TariffProblemKind::Unknown);
        result.ratchet=DemandRatchet{months,std::move(fraction)};
    }
    return result;
}

void validateSeasonalBands(const TariffPlan &plan) {
    if(plan.tiers!=TierMode::Progressive||plan.rounding!=TariffRounding::FinalDisplayOnly||
       std::any_of(plan.revisions.begin(),plan.revisions.end(),[](const TariffRevision &revision){
           return revision.bands.size()!=2||revision.bands.front().throughKwh||revision.bands.back().throughKwh;
       }))
        bad("plan","Seasonal allowance requires two progressive bands with empty bounds and display-only rounding.",
            TariffProblemKind::ModelStatus,{},ElectricityStatus::Unsupported);
}

void normalizeBimonthlyTariff(TariffPlan &plan) {
    if (plan.tiers != TierMode::AllUnits || plan.rounding != TariffRounding::FinalDisplayOnly ||
        plan.revisions.size() != 1 || plan.revisions.front().timeMode != TimeRateMode::Constant ||
        !plan.revisions.front().chargePerCivilDay.zero())
        bad("plan", "A two-month average requires all-units rates, one constant revision, no daily fee and display-only rounding.",
            TariffProblemKind::ModelStatus, {}, ElectricityStatus::Unsupported);

    // Comparing total kWh to twice each monthly boundary is equivalent to comparing the exact monthly average.
    // Multiply flat monthly amounts by two to compute 2 * monthly_bill(total kWh / 2). Unit rates stay unchanged.
    const auto doubleAmount = [](ExactDecimal &value, const std::string &path) {
        if (!value.add(value, &value))
            bad(path, "Arithmetic capacity exceeded.", TariffProblemKind::ArithmeticCapacity);
    };
    doubleAmount(plan.cycleCharge, "plan.cycle_charge");
    if (plan.minimumCycleSubtotal) doubleAmount(*plan.minimumCycleSubtotal, "plan.minimum_cycle_subtotal");
    auto &bands = plan.revisions.front().bands;
    for (size_t i = 0; i < bands.size(); ++i)
        if (bands[i].throughKwh)
            doubleAmount(*bands[i].throughKwh, "plan.revisions[0].bands[" + std::to_string(i) + "].through_kwh");
}

TariffCycleBounds recurringCycle(const TariffResetSchedule &policy, int64_t through) {
    const auto day = policy.day;
    const auto &clock = policy.clock;
    if (!day || day > 31 || policy.annualMonth > 12 || policy.bimonthlyStartMonth > 2 ||
        (policy.annualMonth && policy.bimonthlyStartMonth))
        bad("reset_policy", "The recurring reset requires a valid day and month.", TariffProblemKind::Unknown);
    const auto stamp = civilAt(clock, through);
    if (stamp.status != CivilStatus::Ok)
        bad("opening.through_utc_ms", "Through-time is outside the reset clock range.", TariffProblemKind::Unknown);
    const auto neighbor = [&](CivilDate date, int direction) {
        return monthlyDate(policy.annualMonth ? CivilDate{date.year + direction, policy.annualMonth, 1}
            : offsetMonth(date, policy.bimonthlyStartMonth ? 2 * direction : direction), day);
    };
    auto month = stamp.date;
    if (policy.annualMonth) month.month = policy.annualMonth;
    if (policy.bimonthlyStartMonth && (month.month - 1) % 2 + 1 != policy.bimonthlyStartMonth)
        month = offsetMonth(month, -1);
    auto first = monthlyDate(month, day);
    if (std::tie(stamp.date.year, stamp.date.month, stamp.date.day) < std::tie(first.year, first.month, first.day))
        first = neighbor(first, -1);
    const auto next = neighbor(first, 1);
    const auto boundary = [&](CivilDate date) -> std::optional<int64_t> {
        if (clock.kind != CivilClockKind::NamedIana) return civilResolve(clock, date, 0).utcMs;
        const auto window = civilDay(clock, date);
        if (window.status != CivilStatus::Ok) return std::nullopt;
        return window.beginUtcMs;  // First valid instant of a real reset date.
    };
    auto begin = boundary(first), end = boundary(next);
    if (clock.kind == CivilClockKind::NamedIana && begin && end && through >= *end) {
        // A backward transition can re-enter the preceding local date after the reset.
        begin = end;
        end = boundary(neighbor(next, 1));
    }
    if (!begin || !end || through < *begin || through >= *end)
        bad("opening.through_utc_ms", "The cycle requires two valid reset dates bracketing its through-time.",
            TariffProblemKind::Unknown);
    return {*begin, *end};
}
std::vector<TariffCycleBounds> comparisonCycles(const TariffDeclaration &d,unsigned cycles){
    if(cycles<1||cycles>maxTariffComparisonCycles||(cycles>1&&!d.resetSchedule))return {};
    std::vector<TariffCycleBounds> windows{{d.opening.cycleBeginUtcMs,d.opening.cycleEndUtcMs}};
    try{
        while(windows.size()<cycles){
            const auto boundary=windows.back().endUtcMs;
            const auto next=recurringCycle(*d.resetSchedule,boundary);
            if(next.beginUtcMs!=boundary||next.endUtcMs<=boundary)return {};
            windows.push_back(next);
        }
    }catch(const Invalid &){return {};}
    return windows;
}

bool splitsChargedDay(const TariffPlan &plan,int64_t boundary,std::optional<int64_t> publishedThroughUtcMs=std::nullopt){
    const auto day=civilDayAt(plan.clock,boundary);
    // A reset inside a charged tariff day needs an explicit allocation rule.
    return day.status==CivilStatus::Ok && day.beginUtcMs<boundary &&
        std::any_of(plan.revisions.begin(),plan.revisions.end(),[&](const TariffRevision &r){
            return (!publishedThroughUtcMs || r.publishedUtcMs<=*publishedThroughUtcMs) &&
                !r.chargePerCivilDay.zero() && r.beginUtcMs<day.endUtcMs && r.endUtcMs>day.beginUtcMs;
        });
}

void decodeDemandHistory(const JsonValue &rows,const DemandChargeRule &rule,const TariffResetSchedule &policy,
                         const ExactDecimal &denominator,BillOpening &opening,size_t &total){
    constexpr char field[]="opening.demand_history";
    boundedArray(rows,field,12,total);
    if(rows.items.size()!=rule.ratchet->lookbackMonths)
        bad(field,"Supply one actual peak for each complete month in the lookback.",TariffProblemKind::Unknown);
    opening.demandHistory.resize(rows.items.size());
    auto boundary=opening.cycleBeginUtcMs;
    const int64_t span=rule.windowMinutes*60000LL,offset=rule.offsetMinutes*60000LL;
    const auto *peakField=rule.apparent()?"peak_kva":"peak_kw";
    for(size_t i=rows.items.size();i>0;--i){
        const auto &row=rows.items[i-1];
        const auto path=std::string(field)+"["+std::to_string(i-1)+"]";
        keys(row,path,{"cycle_end_utc_ms",peakField});
        if(boundary<=0||integer(*row.find("cycle_end_utc_ms"),path+".cycle_end_utc_ms")!=static_cast<uint64_t>(boundary))
            bad(path+".cycle_end_utc_ms","Use consecutive completed reset boundaries, oldest first.",TariffProblemKind::Unknown);
        const auto period=recurringCycle(policy,boundary-1);
        if(period.endUtcMs!=boundary||(period.beginUtcMs-offset)%span||(boundary-offset)%span)
            bad(path+".cycle_end_utc_ms","The preceding monthly cycle must align with the demand grid.",TariffProblemKind::Unknown);
        const auto peakPath=child(path,peakField);
        const auto peak=decimal(*row.find(peakField),peakPath);
        if(peak.negative())bad(peakPath,"Use a nonnegative measured peak in the declared demand unit.",TariffProblemKind::Unknown);
        if(!peak.multiply(denominator,&opening.demandHistory[i-1].numerator))
            bad(peakPath,"Arithmetic capacity exceeded.",TariffProblemKind::ArithmeticCapacity);
        boundary=period.beginUtcMs;
    }
}

BillRestrictedCreditOpening decodeRestrictedCredit(const JsonValue &value,const BillOpening &opening,
                                                  const ExactDecimal &denominator,size_t &total){
    constexpr char path[]="restricted_credit";
    constexpr char subtotalPath[]="restricted_credit.accrued_import_subtotal";
    constexpr char lotsPath[]="restricted_credit.lots";
    keys(value,path,{"accrued_import_subtotal","lots"});
    BillRestrictedCreditOpening result;
    const auto subtotal=decimal(*value.find("accrued_import_subtotal"),subtotalPath);
    if(opening.importedKwh.numerator.zero()&&!subtotal.zero())
        bad(subtotalPath,"Enter zero without prior imported energy.",TariffProblemKind::OpeningQuantity);
    if(!subtotal.multiply(denominator,&result.accruedImportSubtotal.numerator))
        bad(subtotalPath,"Arithmetic capacity exceeded.",TariffProblemKind::ArithmeticCapacity);
    const auto &lots=*value.find("lots");
    boundedArray(lots,lotsPath,maxRestrictedCreditLots,total);
    auto previousExpiry=opening.throughUtcMs;
    for(const auto &lot:lots.items){
        const auto row=std::string(lotsPath)+"["+std::to_string(result.lots.size())+"]";
        keys(lot,row,{"expires_utc_ms","amount"});
        const auto expiryPath=child(row,"expires_utc_ms");
        const auto expiry=static_cast<int64_t>(integer(*lot.find("expires_utc_ms"),expiryPath));
        if(expiry<=previousExpiry)
            bad(expiryPath,"Expiry must be after the opening time and after the previous lot.",TariffProblemKind::Unknown);
        const auto amountPath=child(row,"amount");
        const auto amount=decimal(*lot.find("amount"),amountPath);
        if(amount.negative()||amount.zero())
            bad(amountPath,"Use a positive unused credit amount.",TariffProblemKind::OpeningQuantity);
        RestrictedCreditLot decoded;
        decoded.expiresUtcMs=expiry;
        if(!amount.multiply(denominator,&decoded.remaining.numerator))
            bad(amountPath,"Arithmetic capacity exceeded.",TariffProblemKind::ArithmeticCapacity);
        result.lots.push_back(std::move(decoded));
        previousExpiry=expiry;
    }
    return result;
}

bool resetOpening(BillOpening &opening,const TariffCycleBounds &window,const TariffPlan &plan){
    if(plan.demandCharge&&plan.demandCharge->ratchet){
        auto &history=opening.demandHistory;
        if(!opening.peakDemand||history.empty()||history.size()!=plan.demandCharge->ratchet->lookbackMonths)
            return false;
        auto peak=*opening.peakDemand;
        std::rotate(history.begin(),history.begin()+1,history.end());
        history.back()=std::move(peak);
    }
    opening.cycleBeginUtcMs=opening.throughUtcMs=window.beginUtcMs;
    opening.cycleEndUtcMs=window.endUtcMs;
    opening.importedKwh={};
    opening.accruedSubtotal={};
    if(opening.peakDemand)opening.peakDemand=ElectricityAmount{};
    if(opening.exports)opening.exports=BillExportOpening{};
    if(opening.restrictedCredit)opening.restrictedCredit->accruedImportSubtotal={};
    return true;
}
void changes(const JsonValue &a,const JsonValue &b,const std::string &path,std::vector<TariffDeclarationChange> &out){
    if(tariffJson(a)==tariffJson(b))return;
    if(path=="reset_policy"&&!null(a)&&!null(b)&&a.isObject()!=b.isObject()){
        out.push_back({TariffDeclarationChange::Kind::Value,path,tariffJson(a),tariffJson(b),0,0});
        return;
    }
    if(a.isObject()&&b.isObject()){
        for(const auto &m:b.members){const auto *old=a.find(m.first);changes(old?*old:JsonValue{},m.second,child(path,m.first),out);}
        for(const auto &m:a.members)if(!b.find(m.first))changes(m.second,JsonValue{},child(path,m.first),out);
        return;
    }
    if(a.isArray()||b.isArray()){
        if(a.items.size()!=b.items.size())out.push_back({TariffDeclarationChange::Kind::RowCount,path,{},{},a.items.size(),b.items.size()});
        for(size_t i=0;i<(std::max)(a.items.size(),b.items.size());++i)
            changes(i<a.items.size()?a.items[i]:JsonValue{},i<b.items.size()?b.items[i]:JsonValue{},path+"["+std::to_string(i)+"]",out);
        return;
    }
    if(b.isObject()){for(const auto &m:b.members)changes({},m.second,child(path,m.first),out);return;}
    if(a.isObject()){for(const auto &m:a.members)changes(m.second,{},child(path,m.first),out);return;}
    out.push_back({TariffDeclarationChange::Kind::Value,path,tariffJson(a),tariffJson(b),0,0});
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
JsonValue tariffRowTemplate(const std::string &name,bool componentMultiplier,bool apparentDemand){
    if(name=="revisions")return literal(R"({"key":"","begin_utc_ms":0,"end_utc_ms":0,"published_utc_ms":0,"time_mode":"constant","base_rate_per_kwh":"","charge_per_civil_day":"","calendar":[],"dynamic":[],"bands":[]})");
    if(name=="calendar"||name=="dynamic"){
        auto row=literal(name=="calendar"?
            R"({"key":"","begin_minute":0,"end_minute":1440,"weekday_mask":127,"month_mask":4095,"priority":0,"date":null,"published_utc_ms":0,"rate_per_kwh":""})":
            R"({"begin_utc_ms":0,"end_utc_ms":0,"published_utc_ms":0,"rate_per_kwh":""})");
        if(componentMultiplier)row.members.emplace_back("multiplier",literal(R"("")"));
        return row;
    }
    if(name=="lots")return literal(R"({"expires_utc_ms":0,"amount":""})");
    if(name=="bands")return literal(R"({"through_kwh":null,"rate_per_kwh":""})");
    if(name=="seasons")return literal(R"({"start_month":0,"start_day":0,"kwh_per_day":""})");
    if(name=="demand_history")return literal(apparentDemand?R"({"cycle_end_utc_ms":0,"peak_kva":""})":
                                           R"({"cycle_end_utc_ms":0,"peak_kw":""})");
    return {};
}
JsonValue tariffDeclarationTemplate(){
    auto v=literal(R"({"schema_version":1,"country":"","source_reference":"","acknowledged":false,"confirmed_utc":0,"reset_policy":"explicit_cycle_only","plan":{"key":"","territory":"","supplier":"","product":"","currency":"","clock":{"kind":"utc","fixed_offset_seconds":0},"tiers":"none","rounding":"final_display_only","cycle_charge":"","minimum_cycle_subtotal":null,"subtotal_multiplier":"","revisions":[]},"opening":{"key":"","scope":"","adapter":"user-declared-offline","basis":"whole_meter_import","origin":"declared_forecast","cycle_begin_utc_ms":0,"cycle_end_utc_ms":0,"through_utc_ms":0,"imported_kwh":"","accrued_subtotal":"","time_database_version":"explicit-fixed-clock"}})");
    for(auto &m:v.members)if(m.first=="plan")for(auto &p:m.second.members)if(p.first=="revisions")p.second.items.push_back(tariffRowTemplate("revisions"));
    return v;
}
static bool deriveCycle(const JsonValue &policy,const JsonValue &through,TariffCycleBounds &bounds,TariffProblem *problem,TariffResetCadence cadence){
    try{
        const auto at=static_cast<int64_t>(integer(through,"opening.through_utc_ms"));
        bounds=recurringCycle(decodeResetSchedule(policy,true,cadence),at);
        if(problem)*problem={};
        return true;
    }catch(const Invalid &e){if(problem)*problem=e.problem;return false;}
}

bool deriveTariffRecurringCycle(const JsonValue &policy,const JsonValue &through,TariffCycleBounds &bounds,TariffProblem *problem){
    const auto cadence = policy.find("annual_month") ? TariffResetCadence::Annual
        : policy.find("bimonthly_start_month") ? TariffResetCadence::Bimonthly : TariffResetCadence::Monthly;
    return deriveCycle(policy,through,bounds,problem,cadence);
}
bool decodeTariffCivilClock(const JsonValue &input,CivilClock &clock){
    try{
        clock=decodeClock(input,"clock",true);
        return true;
    }catch(const Invalid &){return false;}
}
bool tariffMonthlyCycleAt(unsigned day,const CivilClock &clock,int64_t at,TariffCycleBounds &bounds){
    try{
        bounds=recurringCycle({day,clock},at);
        return true;
    }catch(const Invalid &){return false;}
}
bool deriveTariffMonthlyCycle(const JsonValue &policy,const JsonValue &through,TariffCycleBounds &bounds,TariffProblem *problem){
    return deriveCycle(policy,through,bounds,problem,TariffResetCadence::Monthly);
}

ElectricityStatus seasonalTariffForCycle(const TariffDeclaration &d,const BillOpening &opening,int64_t decision,
                                         std::optional<SeasonalCycleTariff> &adjusted){
    adjusted.reset();
    if(!d.seasonalAllowance)return ElectricityStatus::Ok;
    const auto &rule=*d.seasonalAllowance;
    if(rule.publishedUtcMs>decision)return ElectricityStatus::NotYetKnown;
    if(rule.seasons.empty()||opening.cycleBeginUtcMs>=opening.cycleEndUtcMs)return ElectricityStatus::Invalid;
    auto cursor=opening.cycleBeginUtcMs;
    auto day=civilDayAt(d.plan.clock,cursor);
    const auto closing=civilAt(d.plan.clock,opening.cycleEndUtcMs);
    if(closing.status!=CivilStatus::Ok)return ElectricityStatus::SeasonalDayBoundary;
    ExactDecimal allowance;
    for(unsigned days=0;cursor<opening.cycleEndUtcMs;++days){
        if(days>=370)return ElectricityStatus::Capacity;
        if(day.beginUtcMs!=cursor||day.endUtcMs>opening.cycleEndUtcMs||!civilDayIsContiguous(d.plan.clock,day))
            return ElectricityStatus::SeasonalDayBoundary;
        const auto key=seasonalDateKey(day.date.month,day.date.day);
        const auto selected=std::upper_bound(rule.seasons.begin(),rule.seasons.end(),key,
            [](unsigned dayKey,const SeasonalAllowanceRate &season){
                return dayKey<seasonalDateKey(season.startMonth,season.startDay);
            });
        const auto &season=selected==rule.seasons.begin()?rule.seasons.back():*(selected-1);
        if(!allowance.add(season.kwhPerDay,&allowance))return ElectricityStatus::Capacity;
        cursor=day.endUtcMs;
        const auto next=civilAddDays(day.date,1);
        if(!next)return ElectricityStatus::SeasonalDayBoundary;
        if(cursor==opening.cycleEndUtcMs){
            // A skipped final civil date can share its resolved UTC boundary with the following day.
            if(!sameCivilDate(*next,closing.date))return ElectricityStatus::SeasonalDayBoundary;
        }else day=civilDay(d.plan.clock,*next);
    }
    SeasonalCycleTariff result{d.plan,allowance};
    for(auto &revision:result.plan.revisions){
        if(revision.bands.size()!=2)return ElectricityStatus::Invalid;
        if(allowance.zero())revision.bands.erase(revision.bands.begin());
        else revision.bands.front().throughKwh=allowance;
    }
    adjusted=std::move(result);
    return ElectricityStatus::Ok;
}

std::optional<int64_t> tariffComparisonEnd(const TariffDeclaration &d,unsigned cycles){
    const auto windows=comparisonCycles(d,cycles);
    if(windows.empty())return std::nullopt;
    return windows.back().endUtcMs;
}

ElectricityStatus advanceTariffOpening(const TariffDeclaration &d,BillOpening &settled,int64_t publishedThroughUtcMs,
                                       bool &chargedDaySplit){
    chargedDaySplit=false;
    if(settled.throughUtcMs!=settled.cycleEndUtcMs)return ElectricityStatus::Invalid;
    if(!d.resetSchedule)return ElectricityStatus::Unsupported;
    try{
        const auto boundary=settled.cycleEndUtcMs;
        const auto window=recurringCycle(*d.resetSchedule,boundary);
        if(window.beginUtcMs!=boundary||window.endUtcMs<=boundary)return ElectricityStatus::Invalid;
        chargedDaySplit=d.dailyFeeAllocation()==DailyFeeAllocation::WholeDay&&
            splitsChargedDay(d.plan,boundary,publishedThroughUtcMs);
        if(chargedDaySplit)return ElectricityStatus::Unsupported;
        return resetOpening(settled,window,d.plan)?ElectricityStatus::Ok:ElectricityStatus::Invalid;
    }catch(const Invalid &){
        return ElectricityStatus::Unavailable;
    }
}

TariffCycleComparison compareTariffCycles(const TariffDeclaration &d,const std::array<ExactDecimal,2> &watts,
    unsigned cycles,int64_t decision,const std::atomic<bool> *cancel,
    const std::array<std::optional<ExactDecimal>,2> &apparentVa){
    if(cancel&&cancel->load(std::memory_order_relaxed))return {};
    const auto windows=comparisonCycles(d,cycles);
    if(windows.empty()||d.opening.evidence.basis!=PowerBasis::WholeMeterImport)
        return {ElectricityStatus::Invalid,0,false,{},{},{}};
    if(d.plan.demandCharge&&d.plan.demandCharge->ratchet&&d.opening.cycleBeginUtcMs>decision)
        return {ElectricityStatus::DemandHistoryNotYetKnown,0,false,{},{},{}};
    std::array<BillOpening,2> openings{d.opening,d.opening};
    TariffCycleComparison result;
    for(size_t i=0;i<windows.size();++i){
        if(cancel&&cancel->load(std::memory_order_relaxed))return {};
        const auto cycle=static_cast<unsigned>(i+1);
        const auto &window=windows[i];
        if(i){
            if(d.dailyFeeAllocation()==DailyFeeAllocation::WholeDay&&splitsChargedDay(d.plan,window.beginUtcMs))
                return {ElectricityStatus::Unsupported,cycle,true,{},{},{}};
            for(auto &opening:openings)
                if(!resetOpening(opening,window,d.plan))
                    return {ElectricityStatus::Invalid,cycle,false,{},{},{}};
        }
        std::optional<SeasonalCycleTariff> adjusted;
        const auto status=seasonalTariffForCycle(d,openings[0],decision,adjusted);
        if(status!=ElectricityStatus::Ok)return {status,cycle,false,{},{},{}};
        const auto &plan=adjusted?adjusted->plan:d.plan;
        std::array<ExactDecimal *,2> totals{&result.withoutMining.numerator,&result.withMining.numerator};
        for(size_t scenario=0;scenario<openings.size();++scenario){
            auto &opening=openings[scenario];
            PowerInterval interval{opening.throughUtcMs,window.endUtcMs,watts[scenario],apparentVa[scenario]};
            if(plan.exportPayment){
                auto &exportWatts=interval.exportWatts.emplace();
                if(interval.watts.negative()){
                    if(!exportWatts.subtract(interval.watts,&exportWatts))
                        return {ElectricityStatus::Capacity,cycle,false,{},{},{}};
                    interval.watts={};
                }
            }
            const PowerTrace trace{opening.evidence,{std::move(interval)}};
            auto step=evaluateBill(plan,opening,trace,window.endUtcMs,decision,d.dailyFeeAllocation(),cancel);
            if(cancel&&cancel->load(std::memory_order_relaxed))return {};
            if(step.status!=ElectricityStatus::Ok)return {step.status,cycle,false,{},{},{}};
            ElectricityAmount comparison;
            if(step.restrictedCredit && step.restrictedCredit->cashDue)
                comparison=*step.restrictedCredit->cashDue;
            else if(!step.netChange(comparison))
                return {ElectricityStatus::Capacity,cycle,false,{},{},{}};
            if(!totals[scenario]->add(comparison.numerator,totals[scenario]))
                return {ElectricityStatus::Capacity,cycle,false,{},{},{}};
            opening=std::move(*step.ending);
        }
    }
    if(!result.withMining.numerator.subtract(result.withoutMining.numerator,&result.difference.numerator))
        return {ElectricityStatus::Capacity,0,false,{},{},{}};
    result.status=ElectricityStatus::Ok;
    return result;
}
bool decodeTariffDeclaration(const JsonValue &root,TariffDeclaration *out,TariffProblem *problem){
    try{
        if(!out)bad("","No declaration output.",TariffProblemKind::NoOutput);
        if(tariffJson(root).size()>tariffDeclarationMaxBytes)bad("","Declaration exceeds 64 KiB.",TariffProblemKind::Oversized);
        const auto *allowance=root.find("seasonal_allowance");
        const auto *demand=root.find("demand_charge");
        const auto *restrictedCredit=root.find("restricted_credit");
        using Fields=std::initializer_list<const char *>;
        keys(root,"",{"schema_version","country","source_reference","acknowledged","confirmed_utc","reset_policy","plan","opening"},
             allowance?(demand?Fields{"seasonal_allowance","demand_charge"}:Fields{"seasonal_allowance"}):
                       (demand?Fields{"demand_charge"}:Fields{}),
             restrictedCredit?Fields{"restricted_credit"}:Fields{});
        const auto version=integer(*root.find("schema_version"),"schema_version",13);
        if((version==13)!=static_cast<bool>(restrictedCredit))
            bad("schema_version","Restricted credits require version 13.",TariffProblemKind::SchemaVersion);
        if(!version)bad("schema_version","Unsupported schema.",TariffProblemKind::SchemaVersion);
        const auto &plan=*root.find("plan");
        const auto *exportRule=plan.find("export_payment");
        if((version==12)!=static_cast<bool>(exportRule))
            bad("schema_version","Export payment requires version 12.",TariffProblemKind::SchemaVersion);
        if(version<12&&(version>=9)!=static_cast<bool>(demand))
            bad("schema_version","Demand charge requires version 9.",TariffProblemKind::SchemaVersion);
        const auto *ratchet=demand?demand->find("ratchet"):nullptr;
        const bool apparent=version==11;
        if(version<11&&(version==10)!=static_cast<bool>(ratchet))
            bad("schema_version","Historical kW demand requires version 10.",TariffProblemKind::SchemaVersion);
        const char *openingPeak=apparent?"peak_demand_kva":"peak_demand_kw";
        const bool allowsComponents=version>=8;
        if((version==7&&!allowance)||(version<7&&allowance))
            bad("schema_version","Seasonal allowance requires version 7.",TariffProblemKind::SchemaVersion);
        const auto &policy=*root.find("reset_policy");
        const bool twoMonth=version==6||(allowsComponents&&policy.find("bimonthly_start_month"));
        if(twoMonth&&allowance)
            bad("reset_policy","Seasonal allowance cannot use the two-month average rule.",TariffProblemKind::Unknown);
        if(twoMonth&&demand)
            bad("reset_policy","Demand charge requires a cycle without two-month averaging.",TariffProblemKind::Unknown);
        const bool explicitCycle=version==1||((version==4||version>=7)&&policy.isString());
        if(explicitCycle)choice(policy,"reset_policy",{"explicit_cycle_only"});
        const auto &ack=*root.find("acknowledged");if(!ack.isBool()||!ack.boolean)bad("acknowledged","Acknowledge declared-model exclusions before saving.",TariffProblemKind::Acknowledgement);
        TariffDeclaration d;d.country=label(*root.find("country"),"country");d.sourceReference=label(*root.find("source_reference"),"source_reference");
        d.confirmedUtc=integer(*root.find("confirmed_utc"),"confirmed_utc",lastMs/1000);if(!d.confirmedUtc)bad("confirmed_utc","Explicit review confirmation required.",TariffProblemKind::Confirmation);
        const auto confirmedMs=static_cast<int64_t>(d.confirmedUtc*1000);
        if(allowance)d.seasonalAllowance=decodeSeasonalAllowance(*allowance,confirmedMs);
        auto &p=d.plan;
        if(demand)p.demandCharge=decodeDemandCharge(*demand,confirmedMs,apparent);
        p.restrictedCreditAtClose=restrictedCredit!=nullptr;
        keys(plan,"plan",{"key","territory","supplier","product","currency","clock","tiers","rounding","cycle_charge","minimum_cycle_subtotal","subtotal_multiplier","revisions"},
             exportRule?Fields{"export_payment"}:Fields{});
        if(exportRule){
            const std::string path="plan.export_payment";
            keys(*exportRule,path,{"rate_per_kwh","published_utc_ms"});
            auto rate=decimal(*exportRule->find("rate_per_kwh"),child(path,"rate_per_kwh"));
            if(rate.negative())
                bad(child(path,"rate_per_kwh"),"Use a nonnegative separate payment per exported kWh.",TariffProblemKind::Unknown);
            const auto published=static_cast<int64_t>(integer(*exportRule->find("published_utc_ms"),child(path,"published_utc_ms")));
            if(published>confirmedMs)
                bad(child(path,"published_utc_ms"),"Publication cannot be after local confirmation.",TariffProblemKind::FuturePublication);
            p.exportPayment=ExportPaymentRule{published,std::move(rate)};
        }
        p.declared=true;p.origin=TariffOrigin::UserDeclaredOffline;
        const auto rounding=choice(*plan.find("rounding"),"plan.rounding",{finalDisplayRounding,meterHalfHourRounding});
        p.rounding=rounding==meterHalfHourRounding?TariffRounding::MeterHalfHourKwhHalfEven2:TariffRounding::FinalDisplayOnly;
        for(const auto &binding:std::vector<std::pair<const char *,std::string *>>{{"key",&p.key},{"territory",&p.territory},{"supplier",&p.supplier},{"product",&p.product},{"currency",&p.currency}})
            *binding.second=label(*plan.find(binding.first),child("plan",binding.first));
        if(p.currency.size()!=3||p.currency.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ")!=std::string::npos)bad("plan.currency","Three uppercase letters required; no currency/FX inference.",TariffProblemKind::Currency);
        p.clock=decodeClock(*plan.find("clock"),"plan.clock",version>=4);
        const auto tiers=choice(*plan.find("tiers"),"plan.tiers",{"none","progressive","all_units"});p.tiers=tiers=="none"?TierMode::None:tiers=="progressive"?TierMode::Progressive:TierMode::AllUnits;
        p.cycleCharge=decimal(*plan.find("cycle_charge"),"plan.cycle_charge");
        if(!null(*plan.find("minimum_cycle_subtotal")))p.minimumCycleSubtotal=decimal(*plan.find("minimum_cycle_subtotal"),"plan.minimum_cycle_subtotal");
        p.subtotalMultiplier=decimal(*plan.find("subtotal_multiplier"),"plan.subtotal_multiplier");
        size_t total=d.seasonalAllowance?d.seasonalAllowance->seasons.size():0,index=0;
        boundedArray(*plan.find("revisions"),"plan.revisions",16,total);
        for(const auto &row:plan.find("revisions")->items){
            const auto at="plan.revisions["+std::to_string(index++)+"]";TariffRevision r;
            const auto *components=row.find("component_multiplier");
            if(components&&!allowsComponents)
                bad(at+".component_multiplier","Component multipliers require version 8.",TariffProblemKind::SchemaVersion);
            keys(row,at,{"key","begin_utc_ms","end_utc_ms","published_utc_ms","time_mode","base_rate_per_kwh","charge_per_civil_day","calendar","dynamic","bands"},
                 components?Fields{"component_multiplier"}:Fields{});
            const Fields multiplierField={"multiplier"};
            const auto factorFields=components?multiplierField:Fields{};
            r.key=label(*row.find("key"),child(at,"key"));r.beginUtcMs=integer(*row.find("begin_utc_ms"),child(at,"begin_utc_ms"));r.endUtcMs=integer(*row.find("end_utc_ms"),child(at,"end_utc_ms"));r.publishedUtcMs=integer(*row.find("published_utc_ms"),child(at,"published_utc_ms"));
            const auto mode=choice(*row.find("time_mode"),child(at,"time_mode"),{"constant","calendar","dynamic"});r.timeMode=mode=="constant"?TimeRateMode::Constant:mode=="calendar"?TimeRateMode::Calendar:TimeRateMode::Dynamic;
            r.baseRatePerKwh=decimal(*row.find("base_rate_per_kwh"),child(at,"base_rate_per_kwh"));r.chargePerCivilDay=decimal(*row.find("charge_per_civil_day"),child(at,"charge_per_civil_day"));
            ExactDecimal scaled;
            if(components){
                d.componentMultipliers=true;
                const auto path=at+".component_multiplier";
                keys(*components,path,{"rate_per_kwh","base_multiplier"});
                scaled=decimal(*components->find("rate_per_kwh"),path+".rate_per_kwh");
                r.baseRatePerKwh=componentRate(r.baseRatePerKwh,scaled,*components->find("base_multiplier"),
                                               path+".base_multiplier",r.timeMode==TimeRateMode::Dynamic);
            }
            const auto unitRate=[&](const JsonValue &entry,const std::string &path){
                const auto base=decimal(*entry.find("rate_per_kwh"),path+".rate_per_kwh");
                return components?componentRate(base,scaled,*entry.find("multiplier"),path+".multiplier"):base;
            };
            size_t n=0;
            boundedArray(*row.find("calendar"),child(at,"calendar"),32,total);
            for(const auto &c:row.find("calendar")->items){
                const auto path=at+".calendar["+std::to_string(n++)+"]";CalendarRate rate;
                keys(c,path,{"key","begin_minute","end_minute","weekday_mask","month_mask","priority","date","published_utc_ms","rate_per_kwh"},factorFields);
                rate.key=label(*c.find("key"),child(path,"key"));rate.beginMinute=static_cast<unsigned>(integer(*c.find("begin_minute"),child(path,"begin_minute"),1440));rate.endMinute=static_cast<unsigned>(integer(*c.find("end_minute"),child(path,"end_minute"),1440));
                rate.weekdayMask=static_cast<unsigned>(integer(*c.find("weekday_mask"),child(path,"weekday_mask"),127));rate.monthMask=static_cast<unsigned>(integer(*c.find("month_mask"),child(path,"month_mask"),4095));rate.priority=static_cast<unsigned>(integer(*c.find("priority"),child(path,"priority"),1000));
                const auto &date=*c.find("date");if(!null(date)){
                    const auto s=label(date,child(path,"date"));
                    if(s.size()!=10||s[4]!='-'||s[7]!='-'||s.substr(0,4).find_first_not_of("0123456789")!=std::string::npos||s.substr(5,2).find_first_not_of("0123456789")!=std::string::npos||s.substr(8).find_first_not_of("0123456789")!=std::string::npos)bad(child(path,"date"),"Use YYYY-MM-DD or clear for no exact-date condition.",TariffProblemKind::Date);
                    rate.date=CivilDate{std::stoi(s.substr(0,4)),static_cast<unsigned>(std::stoi(s.substr(5,2))),static_cast<unsigned>(std::stoi(s.substr(8,2)))};
                }
                rate.publishedUtcMs=integer(*c.find("published_utc_ms"),child(path,"published_utc_ms"));rate.ratePerKwh=unitRate(c,path);r.calendar.push_back(std::move(rate));
            }
            boundedArray(*row.find("dynamic"),child(at,"dynamic"),256,total);
            n=0;for(const auto &v:row.find("dynamic")->items){
                const auto path=at+".dynamic["+std::to_string(n++)+"]";keys(v,path,{"begin_utc_ms","end_utc_ms","published_utc_ms","rate_per_kwh"},factorFields);
                r.dynamic.push_back({static_cast<int64_t>(integer(*v.find("begin_utc_ms"),child(path,"begin_utc_ms"))),static_cast<int64_t>(integer(*v.find("end_utc_ms"),child(path,"end_utc_ms"))),static_cast<int64_t>(integer(*v.find("published_utc_ms"),child(path,"published_utc_ms"))),unitRate(v,path)});
            }
            boundedArray(*row.find("bands"),child(at,"bands"),32,total);
            n=0;for(const auto &v:row.find("bands")->items){
                const auto path=at+".bands["+std::to_string(n++)+"]";keys(v,path,{"through_kwh","rate_per_kwh"});TariffBand band;
                if(!null(*v.find("through_kwh")))band.throughKwh=decimal(*v.find("through_kwh"),child(path,"through_kwh"));
                band.ratePerKwh=decimal(*v.find("rate_per_kwh"),child(path,"rate_per_kwh"));r.bands.push_back(std::move(band));
            }
            p.revisions.push_back(std::move(r));
        }
        if(version==8&&!d.componentMultipliers)
            bad("schema_version","Version 8 requires a revision with a component multiplier.",TariffProblemKind::SchemaVersion);
        auto &s=d.opening;const auto &opening=*root.find("opening");
        keys(opening,"opening",{"key","scope","adapter","basis","origin","cycle_begin_utc_ms","cycle_end_utc_ms","through_utc_ms","imported_kwh","accrued_subtotal","time_database_version"},
             exportRule?(ratchet?Fields{openingPeak,"demand_history","exported_kwh","accrued_export_payment"}:
                                   demand?Fields{openingPeak,"exported_kwh","accrued_export_payment"}:
                                          Fields{"exported_kwh","accrued_export_payment"}):
                        (ratchet?Fields{openingPeak,"demand_history"}:demand?Fields{openingPeak}:Fields{}));
        choice(*opening.find("basis"),"opening.basis",{"whole_meter_import"});choice(*opening.find("origin"),"opening.origin",{"declared_forecast"});
        s.declared=true;s.planKey=p.key;s.evidence={PowerBasis::WholeMeterImport,PowerOrigin::DeclaredForecast,label(*opening.find("scope"),"opening.scope"),label(*opening.find("adapter"),"opening.adapter")};
        d.openingKey=label(*opening.find("key"),"opening.key");
        const bool namedRate=p.clock.kind==CivilClockKind::NamedIana;
        s.timeDatabaseVersion=choice(*opening.find("time_database_version"),"opening.time_database_version",
            {namedRate?tariffTzdbVersion:"explicit-fixed-clock"});
        s.cycleBeginUtcMs=integer(*opening.find("cycle_begin_utc_ms"),"opening.cycle_begin_utc_ms");s.cycleEndUtcMs=integer(*opening.find("cycle_end_utc_ms"),"opening.cycle_end_utc_ms");s.throughUtcMs=integer(*opening.find("through_utc_ms"),"opening.through_utc_ms");
        if(!explicitCycle){
            const auto cadence = twoMonth ? TariffResetCadence::Bimonthly
                : version == 5 || (version >= 7 && policy.find("annual_month")) ? TariffResetCadence::Annual
                : TariffResetCadence::Monthly;
            d.resetSchedule=decodeResetSchedule(policy,version>=4,cadence);
            if(version<4&&(version==3)!=(d.dailyFeeAllocation()==DailyFeeAllocation::ProrateByCycleMinutes))
                bad("reset_policy.daily_fee_allocation","The chosen daily fee rule must match the schema version.",TariffProblemKind::Unknown);
            const auto bounds=recurringCycle(*d.resetSchedule,s.throughUtcMs);
            if(s.cycleBeginUtcMs!=bounds.beginUtcMs||s.cycleEndUtcMs!=bounds.endUtcMs)
                bad("opening","Derive both cycle boundaries from the reset rule and through-time.",TariffProblemKind::Unknown);
        }
        if(ratchet&&(!d.resetSchedule||d.resetSchedule->annualMonth||d.resetSchedule->bimonthlyStartMonth))
            bad("reset_policy","Demand ratchet requires monthly resets.",TariffProblemKind::Unknown);
        const bool namedReset=d.resetSchedule&&d.resetSchedule->clock.kind==CivilClockKind::NamedIana;
        if(version==4&&!namedRate&&!namedReset)
            bad("schema_version","Version 4 requires an explicitly named tariff or reset clock.",TariffProblemKind::SchemaVersion);
        auto quantity=electricityKwh(decimal(*opening.find("imported_kwh"),"opening.imported_kwh"));if(!quantity)bad("opening.imported_kwh","Nonnegative whole-meter quantity required.",TariffProblemKind::OpeningQuantity);s.importedKwh=*quantity;
        ExactDecimal denominator;ExactDecimal::parse("3600000000",&denominator);
        if(!decimal(*opening.find("accrued_subtotal"),"opening.accrued_subtotal").multiply(denominator,&s.accruedSubtotal.numerator))bad("opening.accrued_subtotal","Arithmetic capacity exceeded.",TariffProblemKind::ArithmeticCapacity);
        if(exportRule){
            constexpr char path[]="opening.exported_kwh";
            auto exported=electricityKwh(decimal(*opening.find("exported_kwh"),path));
            if(!exported||(s.throughUtcMs==s.cycleBeginUtcMs&&!exported->numerator.zero()))
                bad(path,"Use nonnegative exported kWh and enter zero at cycle start.",TariffProblemKind::OpeningQuantity);
            constexpr char paymentPath[]="opening.accrued_export_payment";
            auto paid=decimal(*opening.find("accrued_export_payment"),paymentPath);
            if(paid.negative()||(exported->numerator.zero()&&!paid.zero()))
                bad(paymentPath,"Use a nonnegative accrued export payment. Enter zero without prior exports.",TariffProblemKind::OpeningQuantity);
            ElectricityAmount accrued;
            if(!paid.multiply(denominator,&accrued.numerator))
                bad(paymentPath,"Arithmetic capacity exceeded.",TariffProblemKind::ArithmeticCapacity);
            s.exports=BillExportOpening{std::move(*exported),std::move(accrued)};
        }
        if(restrictedCredit)s.restrictedCredit=decodeRestrictedCredit(*restrictedCredit,s,denominator,total);
        if(p.demandCharge){
            s.apparentDemand=apparent;
            const auto path=child("opening",openingPeak);
            const auto peak=decimal(*opening.find(openingPeak),path);
            if(peak.negative())bad(path,"Use a nonnegative established peak in the declared demand unit.",TariffProblemKind::Unknown);
            if(!peak.multiply(denominator,&s.peakDemand.emplace().numerator))
                bad(path,"Arithmetic capacity exceeded.",TariffProblemKind::ArithmeticCapacity);
            if(ratchet){
                if(s.cycleBeginUtcMs>confirmedMs)
                    bad("opening.demand_history","The latest historical month must be complete at local confirmation.",TariffProblemKind::Unknown);
                decodeDemandHistory(*opening.find("demand_history"),*p.demandCharge,*d.resetSchedule,denominator,s,total);
            }
            if(s.throughUtcMs!=s.cycleBeginUtcMs){
                ElectricityAmount billable,floor,peakCharge;
                if(!billableDemand(*p.demandCharge,s,billable,floor)||
                   !p.demandCharge->charge(billable,&peakCharge)||
                   !s.accruedSubtotal.numerator.add(peakCharge.numerator,&s.accruedSubtotal.numerator))
                    bad(path,"Arithmetic capacity exceeded.",TariffProblemKind::ArithmeticCapacity);
            }
        }
        if(twoMonth)normalizeBimonthlyTariff(p);
        std::optional<SeasonalCycleTariff> adjusted;
        if(allowance){
            validateSeasonalBands(p);
            const auto status=seasonalTariffForCycle(d,s,confirmedMs,adjusted);
            if(status!=ElectricityStatus::Ok)
                bad("opening",electricityStatusName(status),TariffProblemKind::ModelStatus,{},status);
        }
        const auto status=validateTariffOpening(adjusted?adjusted->plan:p,s);if(status!=ElectricityStatus::Ok)bad("plan/opening",electricityStatusName(status),TariffProblemKind::ModelStatus,{},status);
        // This store is an offline declaration, so publication is a local claim bounded by confirmation, not certification.
        for(const auto &r:p.revisions){
            if(r.publishedUtcMs>static_cast<int64_t>(d.confirmedUtc*1000))bad("plan.revisions","Publication cannot be after local confirmation.",TariffProblemKind::FuturePublication);
            for(const auto &c:r.calendar)if(c.publishedUtcMs>static_cast<int64_t>(d.confirmedUtc*1000))bad("plan.revisions.calendar","Publication cannot be after local confirmation.",TariffProblemKind::FuturePublication);
            for(const auto &v:r.dynamic)if(v.publishedUtcMs>static_cast<int64_t>(d.confirmedUtc*1000))bad("plan.revisions.dynamic","Publication cannot be after local confirmation.",TariffProblemKind::FuturePublication);
        }
        *out=std::move(d);if(problem)*problem={};return true;
    }catch(const Invalid &e){if(problem)*problem=e.problem;return false;}
}
bool parseTariffDeclaration(const std::string &text,JsonValue *root,TariffProblem *problem){
    JsonValue v;TariffDeclaration d;
    if(!root||text.size()>tariffDeclarationMaxBytes||!parseJsonStrict(text,&v,nullptr)){if(problem)*problem={"","Invalid/oversized JSON; file preserved.",TariffProblemKind::InvalidJson,{},{}};return false;}
    if(!decodeTariffDeclaration(v,&d,problem))return false;
    *root=std::move(v);return true;
}
std::vector<TariffDeclarationChange> tariffDeclarationChangeRecords(const JsonValue &a,const JsonValue &b){std::vector<TariffDeclarationChange> result;changes(a,b,"",result);return result;}
std::vector<std::string> tariffDeclarationChanges(const JsonValue &a,const JsonValue &b){std::vector<std::string> result;
    for(const auto &record:tariffDeclarationChangeRecords(a,b))result.push_back(record.kind==TariffDeclarationChange::Kind::RowCount?
        record.path+" rows: "+std::to_string(record.beforeRows)+" -> "+std::to_string(record.afterRows):record.path+": "+record.beforeJson+" -> "+record.afterJson);
    return result;
}
} // namespace om
