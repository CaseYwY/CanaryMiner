#include "product/tariff_settings.h"
#include "product/settings.h"
#include "product_test_util.h"
#include <chrono>
#include <fstream>
#include <sstream>
using namespace om;
namespace {
constexpr uint64_t T=1800000000000ULL;
JsonValue &member(JsonValue &v,const char *key){for(auto &m:v.members)if(m.first==key)return m.second;throw 1;}
JsonValue json(const std::string &text){JsonValue v;CHECK(parseJsonStrict(text,&v,nullptr));return v;}
void set(JsonValue &v,const char *key,const std::string &encoded){member(v,key)=json(encoded);}
ExactDecimal dec(const char *s){ExactDecimal v;CHECK(ExactDecimal::parse(s,&v));return v;}
JsonValue fixture(){std::ifstream in("config/tariff-declaration.example.json");std::stringstream text;text<<in.rdbuf();JsonValue value;TariffProblem error;CHECK(parseTariffDeclaration(text.str(),&value,&error));return value;}
void enter(TariffSettingsPanel &p,const char *name){
    JsonValue *v=&p.draft;for(size_t i:p.route)v=v->isObject()?&v->members[i].second:&v->items[i];
    size_t i=0;for(;i<v->members.size();++i)if(v->members[i].first==name)break;CHECK(i<v->members.size());p.field=i;p.key('\r',{},T/1000);
}
void type(TariffSettingsPanel &p,const std::string &text){for(unsigned char c:text)p.key(c,{},T/1000);p.key('\r',{},T/1000);}
void frame(const TariffSettingsPanel &p,int columns,int rows){const auto out=p.render(columns,rows,ColorLevel::None);CHECK(out.find('\x1b')==std::string::npos);std::istringstream lines(out);std::string line;int n=0;while(std::getline(lines,line)){CHECK(line.size()<static_cast<size_t>(columns));++n;}CHECK(n<=rows);}
std::string awaitComparison(const TariffSettingsPanel &panel) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    for (;;) {
        auto rendered = panel.render(60,12,ColorLevel::None);
        if (rendered.find("Calculating the saved scenario...") == std::string::npos) return rendered;
        if (std::chrono::steady_clock::now() >= deadline) { CHECK(false); return rendered; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void checkComparison(const TariffCycleComparison &value,const char *base,const char *with,const char *delta){
    CHECK(value.status==ElectricityStatus::Ok);
    std::string amount;
    CHECK(value.withoutMining.format(6,&amount)&&amount==base);
    CHECK(value.withMining.format(6,&amount)&&amount==with);
    CHECK(value.difference.format(6,&amount)&&amount==delta);
}

void comparisonHorizon(JsonValue saved){
    constexpr int64_t march=1711843200000, april=1714435200000, nextFebruary=1740700800000;
    const std::array<ExactDecimal,2> watts{dec("0"),dec("1000")};
    TariffDeclaration declaration;
    CHECK(decodeTariffDeclaration(saved,&declaration));
    CHECK(declaration.resetSchedule&&declaration.resetSchedule->day==31);
    CHECK(tariffComparisonEnd(declaration,1)==march&&tariffComparisonEnd(declaration,2)==april);
    CHECK(tariffComparisonEnd(declaration,12)==nextFebruary);
    CHECK(!tariffComparisonEnd(declaration,0)&&!tariffComparisonEnd(declaration,13));
    const auto unavailable=compareTariffCycles(declaration,watts,2,T);
    CHECK(unavailable.status==ElectricityStatus::Unavailable&&unavailable.failedCycle==2);
    CHECK(unavailable.withoutMining.numerator.zero()&&unavailable.withMining.numerator.zero()&&unavailable.difference.numerator.zero());

    auto &plan=member(saved,"plan");
    auto &revision=member(plan,"revisions").items[0];
    set(revision,"end_utc_ms",std::to_string(april));
    CHECK(decodeTariffDeclaration(saved,&declaration));
    checkComparison(compareTariffCycles(declaration,watts,2,T),"2.200000","165.880000","163.680000");
    CHECK(declaration.opening.cycleEndUtcMs==march&&!declaration.opening.importedKwh.numerator.zero());

    TariffSettingsPanel panel;
    panel.open(saved);
    panel.key('p',{},T/1000);
    panel.key(kKeyRight,{},T/1000);
    CHECK(panel.render(60,12,ColorLevel::None).find("Cycles 2/12")!=std::string::npos);
    CHECK(panel.render(60,12,ColorLevel::None).find(std::to_string(april))!=std::string::npos);
    for(char c:std::string("0\t1000\r"))panel.key(c,{},T/1000);
    CHECK(awaitComparison(panel).find("163.680000 USD")!=std::string::npos);

    auto large=saved;
    set(large,"schema_version","3");
    member(large,"reset_policy").members.emplace_back("daily_fee_allocation",json("\"prorate_by_cycle_minutes\""));
    auto &largeRate=member(member(large,"plan"),"revisions").items.front();
    set(largeRate,"base_rate_per_kwh","\"123456789012345678901234567890\"");
    TariffSettingsPanel narrowPanel;
    narrowPanel.open(large);
    narrowPanel.key('p',{},T/1000);
    for(char c:std::string("1000\t4000\r"))narrowPanel.key(c,{},T/1000);
    const auto narrowResult=awaitComparison(narrowPanel);
    CHECK(narrowResult.find("Difference (with - without)")!=std::string::npos);
    CHECK(narrowResult.find("[Esc] Back [Q] Stop")!=std::string::npos);
    frame(narrowPanel,60,12);
    frame(panel,60,12);
    panel.key(kKeyRight,{},T/1000);
    CHECK(panel.render(60,12,ColorLevel::None).find("163.680000 USD")==std::string::npos);
    panel.key('\r',{},T/1000);
    const auto failed=awaitComparison(panel);
    CHECK(failed.find("Comparison unavailable in cycle 3")!=std::string::npos);
    CHECK(failed.find("Remaining with mining:")==std::string::npos);
    panel.key(kKeyLeft,{},T/1000);
    panel.key(kKeyLeft,{},T/1000);
    panel.key('\r',{},T/1000);
    CHECK(awaitComparison(panel).find("5.280000 USD")!=std::string::npos);
    panel.key(27,{},T/1000);
    CHECK(tariffJson(panel.active)==tariffJson(saved)&&!panel.dirty());

    auto progressive=declaration;
    progressive.plan.tiers=TierMode::Progressive;
    progressive.plan.revisions[0].bands={{dec("10"),dec("0.05")},{std::nullopt,dec("0.15")}};
    checkComparison(compareTariffCycles(progressive,watts,2,T),"2.200000","287.540000","285.340000");
    progressive.plan.tiers=TierMode::AllUnits;
    checkComparison(compareTariffCycles(progressive,watts,2,T),"2.200000","288.640000","286.440000");
    auto minimum=declaration;
    minimum.plan.minimumCycleSubtotal=dec("8");
    checkComparison(compareTariffCycles(minimum,watts,2,T),"15.400000","167.200000","151.800000");

    auto fee=declaration;
    fee.plan.revisions[0].chargePerCivilDay=dec("0.1");
    checkComparison(compareTariffCycles(fee,watts,2,T),"5.610000","169.290000","163.680000");
    fee.resetSchedule->clock={CivilClockKind::FixedStandardOffset,{},19800};
    fee.opening.cycleBeginUtcMs-=19800000;
    fee.opening.cycleEndUtcMs-=19800000;
    const auto split=compareTariffCycles(fee,watts,2,T);
    CHECK(split.status==ElectricityStatus::Unsupported&&split.failedCycle==2&&split.splitsChargedDay);
    fee.resetSchedule->dailyFeeAllocation=DailyFeeAllocation::ProrateByCycleMinutes;
    fee.plan.revisions[0].chargePerCivilDay=dec("1.44");
    const auto prorated=compareTariffCycles(fee,watts,2,T);
    CHECK(prorated.status==ElectricityStatus::Ok&&!prorated.splitsChargedDay);
    fee.resetSchedule->dailyFeeAllocation=DailyFeeAllocation::WholeDay;
    fee.plan.revisions[0].chargePerCivilDay={};
    const auto unaligned=compareTariffCycles(fee,watts,2,T);
    CHECK(unaligned.status==ElectricityStatus::Ok&&tariffComparisonEnd(fee,2)==april-19800000);
    CHECK(prorated.difference.numerator.compare(unaligned.difference.numerator)==0);
    CHECK(prorated.withoutMining.numerator.compare(unaligned.withoutMining.numerator)>0);

    auto dynamic=declaration;
    auto &rates=dynamic.plan.revisions[0];
    rates.timeMode=TimeRateMode::Dynamic;
    rates.baseRatePerKwh={};
    rates.dynamic={{rates.beginUtcMs,march,rates.publishedUtcMs,dec("0.2")}};
    const auto gap=compareTariffCycles(dynamic,watts,2,T);
    CHECK(gap.status==ElectricityStatus::MissingPrice&&gap.failedCycle==2&&gap.withMining.numerator.zero());
    rates.dynamic.push_back({march,april,static_cast<int64_t>(T)+1,dec("0.2")});
    const auto late=compareTariffCycles(dynamic,watts,2,T);
    CHECK(late.status==ElectricityStatus::NotYetKnown&&late.failedCycle==2&&late.withMining.numerator.zero());
    const std::atomic<bool> cancelled{true};
    CHECK(compareTariffCycles(declaration,watts,2,T,&cancelled).status==ElectricityStatus::Unavailable);
    declaration.resetSchedule.reset();
    CHECK(!tariffComparisonEnd(declaration,2));
}

void annualCycles(const JsonValue &original) {
    constexpr int64_t feb2023=1677542400000, feb2024=1709164800000, feb2025=1740700800000, feb2026=1772236800000;
    const auto policy=json(R"({"annual_month":2,"annual_day":29,"clock":{"kind":"utc","fixed_offset_seconds":0}})");
    TariffCycleBounds bounds;
    TariffProblem error;
    CHECK(deriveTariffRecurringCycle(policy,json(std::to_string(feb2024-1)),bounds,&error));
    CHECK(bounds.beginUtcMs==feb2023&&bounds.endUtcMs==feb2024);
    CHECK(deriveTariffRecurringCycle(policy,json(std::to_string(feb2024)),bounds,&error));
    CHECK(bounds.beginUtcMs==feb2024&&bounds.endUtcMs==feb2025);
    CHECK(!deriveTariffMonthlyCycle(policy,json(std::to_string(feb2024)),bounds,&error));
    auto fixed=policy;
    set(fixed,"clock",R"({"kind":"fixed_standard","fixed_offset_seconds":-19800})");
    CHECK(deriveTariffRecurringCycle(fixed,json(std::to_string(feb2024+19800000)),bounds,&error));
    CHECK(bounds.beginUtcMs==feb2024+19800000&&bounds.endUtcMs==feb2025+19800000);
    const auto prior=bounds;
    for(const auto *invalid:{"0","13","true","2.0","\"2\""}){
        auto bad=policy;
        set(bad,"annual_month",invalid);
        CHECK(!deriveTariffRecurringCycle(bad,json(std::to_string(feb2024)),bounds,&error));
        CHECK(error.path=="reset_policy.annual_month"&&bounds.beginUtcMs==prior.beginUtcMs&&bounds.endUtcMs==prior.endUtcMs);
    }

    auto v=original;
    auto &plan=member(v,"plan");
    auto &opening=member(v,"opening");
    auto &revision=member(plan,"revisions").items[0];
    set(plan,"tiers","\"none\"");
    set(plan,"cycle_charge","\"10\"");
    set(plan,"minimum_cycle_subtotal","null");
    set(plan,"subtotal_multiplier","\"1\"");
    set(revision,"base_rate_per_kwh","\"0.1\"");
    set(revision,"time_mode","\"constant\"");
    set(revision,"charge_per_civil_day","\"0\"");
    set(revision,"calendar","[]");
    set(revision,"bands","[]");
    set(revision,"begin_utc_ms",std::to_string(feb2024));
    set(revision,"end_utc_ms",std::to_string(feb2026));
    set(revision,"published_utc_ms","1700000000000");
    set(opening,"cycle_begin_utc_ms",std::to_string(feb2024));
    set(opening,"cycle_end_utc_ms",std::to_string(feb2025));
    set(opening,"through_utc_ms",std::to_string(feb2024));

    TariffSettingsPanel panel;
    panel.open(v);
    enter(panel,"reset_policy");
    CHECK(panel.render(85,18,ColorLevel::None).find("[Y] Annual reset")!=std::string::npos);
    panel.key('y',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="5"&&panel.active.find("schema_version")->text=="1");
    CHECK(panel.draft.find("opening")->find("imported_kwh")->text.empty());
    CHECK(panel.draft.find("opening")->find("accrued_subtotal")->text.empty());
    enter(panel,"reset_policy");
    enter(panel,"annual_month");type(panel,"2");
    enter(panel,"annual_day");type(panel,"29");
    panel.key('g',{},T/1000);
    CHECK(panel.path()=="opening"&&panel.message.find("Draft cycle dates derived")!=std::string::npos);
    enter(panel,"imported_kwh");type(panel,"0");
    enter(panel,"accrued_subtotal");type(panel,"0");
    panel.key('v',{},T/1000);
    CHECK(panel.render(120,100,ColorLevel::None).find("Usage carries across months.")!=std::string::npos);
    TariffDeclaration decoded;
    CHECK(decodeTariffDeclaration(panel.draft,&decoded,&error));
    CHECK(decoded.resetSchedule&&decoded.resetSchedule->annualMonth==2);
    CHECK(tariffComparisonEnd(decoded,2)==feb2026);
    const std::array<ExactDecimal,2> watts{dec("0"),dec("1000")};
    checkComparison(compareTariffCycles(decoded,watts,2,T),"20.000000","1772.000000","1752.000000");
    auto settled=decoded.opening;
    settled.throughUtcMs=feb2025;
    settled.importedKwh=*electricityKwh(dec("20"));
    bool chargedDaySplit=false;
    CHECK(advanceTariffOpening(decoded,settled,T,chargedDaySplit)==ElectricityStatus::Ok);
    CHECK(settled.cycleBeginUtcMs==feb2025&&settled.cycleEndUtcMs==feb2026&&settled.importedKwh.numerator.zero());

    panel.open(panel.draft);
    enter(panel,"reset_policy");
    panel.key(27,{},T/1000);
    panel.key('m',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="2");
    CHECK(panel.draft.find("reset_policy")->find("monthly_day")->text=="29");
    CHECK(panel.draft.find("opening")->find("imported_kwh")->text.empty());
}

void componentMultipliers(JsonValue draft,const std::filesystem::path &target) {
    auto &plan=member(draft,"plan");
    set(plan,"tiers",R"("none")");
    set(plan,"cycle_charge",R"("0")");
    set(plan,"subtotal_multiplier",R"("1")");
    auto &row=member(plan,"revisions").items.front();
    set(row,"base_rate_per_kwh",R"("3")");
    set(row,"charge_per_civil_day",R"("0")");
    set(row,"bands","[]");
    set(member(row,"calendar").items.front(),"rate_per_kwh",R"("3")");

    std::string reason;
    auto store=TariffDeclarationStore(target);
    JsonValue initial;
    CHECK(store.load(&initial,&reason)&&store.save(draft,&reason));
    TariffSettingsPanel panel;
    panel.open(draft);
    enter(panel,"plan");
    enter(panel,"revisions");
    panel.key('\r',{},T/1000);
    enter(panel,"base_rate_per_kwh");
    panel.key(27,{},T/1000);
    CHECK(panel.render(120,24,ColorLevel::None).find("[C] Component multiplier")!=std::string::npos);
    panel.key('c',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="8");
    enter(panel,"component_multiplier");
    enter(panel,"rate_per_kwh");type(panel,"2");
    enter(panel,"base_multiplier");type(panel,"1");
    panel.key(27,{},T/1000);
    enter(panel,"calendar");
    panel.key('a',{},T/1000);
    CHECK(panel.draft.find("plan")->find("revisions")->items.front().find("calendar")->items.back().find("multiplier"));
    panel.key('x',{},T/1000);
    panel.key('\r',{},T/1000);
    panel.key('\r',{},T/1000);
    enter(panel,"multiplier");type(panel,"2");
    panel.key('v',{},T/1000);
    CHECK(panel.render(120,28,ColorLevel::None).find("Unit rate = unscaled part + multiplied part x factor")!=std::string::npos);
    const auto save=[&](const JsonValue &candidate,std::string *error){return store.save(candidate,error);};
    panel.key('\r',save,T/1000);
    CHECK(panel.message.find("NEW plan.key")!=std::string::npos);
    auto saved=panel.draft;
    set(member(saved,"plan"),"key",R"("component-plan")");
    CHECK(!store.save(saved,&reason)&&reason.find("NEW opening.key")!=std::string::npos);
    set(member(saved,"opening"),"key",R"("component-opening")");
    CHECK(store.save(saved,&reason)&&store.load(&initial,&reason));
    CHECK(tariffJson(initial)==tariffJson(saved));

    TariffDeclaration declaration;
    CHECK(decodeTariffDeclaration(saved,&declaration)&&declaration.componentMultipliers);
    CHECK(declaration.plan.revisions.front().baseRatePerKwh.text()=="5");
    CHECK(declaration.plan.revisions.front().calendar.front().ratePerKwh.text()=="7");
    const std::array<ExactDecimal,2> watts{dec("0"),dec("1000")};
    checkComparison(compareTariffCycles(declaration,watts,1,T),"0.000000","128.000000","128.000000");

    auto &opening=member(saved,"opening");
    set(opening,"through_utc_ms",std::to_string(T+3600000));
    set(opening,"imported_kwh",R"("1")");
    set(opening,"accrued_subtotal",R"("5")");
    panel.open(saved);
    enter(panel,"plan");
    enter(panel,"revisions");
    panel.key('\r',{},T/1000);
    enter(panel,"component_multiplier");
    enter(panel,"rate_per_kwh");type(panel,"2.5");
    CHECK(panel.draft.find("opening")->find("imported_kwh")->text=="1");
    CHECK(panel.draft.find("opening")->find("accrued_subtotal")->text.empty());
    panel.open(saved);
    enter(panel,"plan");
    enter(panel,"cycle_charge");type(panel,"1");
    CHECK(panel.draft.find("opening")->find("imported_kwh")->text=="1");
    CHECK(panel.draft.find("opening")->find("accrued_subtotal")->text.empty());
    panel.open(saved);
    enter(panel,"plan");
    enter(panel,"rounding");
    CHECK(panel.draft.find("opening")->find("imported_kwh")->text=="1");
    CHECK(panel.draft.find("opening")->find("accrued_subtotal")->text.empty());
    panel.open(saved);
    enter(panel,"opening");
    enter(panel,"through_utc_ms");type(panel,std::to_string(T+7200000));
    CHECK(panel.draft.find("opening")->find("imported_kwh")->text.empty());
    CHECK(panel.draft.find("opening")->find("accrued_subtotal")->text.empty());
    panel.open(saved);
    enter(panel,"plan");
    enter(panel,"revisions");
    panel.key('\r',{},T/1000);
    enter(panel,"base_rate_per_kwh");
    panel.key(27,{},T/1000);
    panel.key('c',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="1");
    CHECK(!panel.draft.find("plan")->find("revisions")->items.front().find("calendar")->items.front().find("multiplier"));
    for(int depth=0;depth<3;++depth)panel.key(27,{},T/1000);
    enter(panel,"opening");
    enter(panel,"accrued_subtotal");type(panel,"3");
    CHECK(panel.draft.find("opening")->find("accrued_subtotal")->text=="3");
    panel.key(27,{},T/1000);
    enter(panel,"plan");
    enter(panel,"cycle_charge");type(panel,"1");
    CHECK(panel.draft.find("opening")->find("imported_kwh")->text=="1");
    CHECK(panel.draft.find("opening")->find("accrued_subtotal")->text.empty());
    panel.key(27,{},T/1000);
    enter(panel,"opening");
    enter(panel,"scope");type(panel,"replacement-meter");
    CHECK(panel.draft.find("opening")->find("imported_kwh")->text.empty());
    auto missing=saved;
    set(missing,"schema_version","7");
    TariffProblem problem;
    CHECK(!decodeTariffDeclaration(missing,&declaration,&problem));
    CHECK(problem.path=="schema_version"&&problem.message=="Seasonal allowance requires version 7.");
}

void demandWindows(JsonValue draft,const std::filesystem::path &target) {
    const auto middle=T+15*60000,close=middle+15*60000;
    auto &plan=member(draft,"plan");
    set(plan,"tiers",R"("none")");
    set(plan,"cycle_charge",R"("0")");
    set(plan,"subtotal_multiplier",R"("1")");
    auto &row=member(plan,"revisions").items.front();
    set(row,"end_utc_ms",std::to_string(close));
    set(row,"time_mode",R"("constant")");
    set(row,"base_rate_per_kwh",R"("0")");
    set(row,"charge_per_civil_day",R"("0")");
    set(row,"calendar","[]");
    set(row,"bands","[]");
    set(member(draft,"opening"),"cycle_end_utc_ms",std::to_string(close));

    TariffSettingsPanel panel;
    panel.open(draft);
    enter(panel,"reset_policy");
    CHECK(panel.render(120,26,ColorLevel::None).find("[D] Demand charge")!=std::string::npos);
    panel.key('d',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="9");
    enter(panel,"demand_charge");
    enter(panel,"window_minutes");type(panel,"15");
    enter(panel,"rate_per_kw");type(panel,"5");
    panel.key(27,{},T/1000);
    enter(panel,"opening");
    enter(panel,"peak_demand_kw");type(panel,"0");
    panel.key('v',{},T/1000);
    CHECK(panel.render(120,27,ColorLevel::None).find("The opening subtotal excludes it")!=std::string::npos);

    auto store=TariffDeclarationStore(target);
    std::string reason;
    JsonValue saved;
    CHECK(store.load(&saved,&reason));
    panel.key('\r',[&](const JsonValue &value,std::string *error){return store.save(value,error);},T/1000);
    CHECK(store.load(&saved,&reason)&&saved.find("schema_version")->text=="9");
    TariffDeclaration decoded;
    CHECK(decodeTariffDeclaration(saved,&decoded)&&decoded.plan.demandCharge);
    checkComparison(compareTariffCycles(decoded,{dec("0"),dec("4000")},1,T),"0.000000","20.000000","20.000000");

    auto changed=saved;
    set(member(changed,"demand_charge"),"rate_per_kw",R"("6")");
    CHECK(!store.save(changed,&reason)&&reason.find("NEW plan.key")!=std::string::npos);
    set(member(changed,"plan"),"key",R"("new-demand-plan")");
    CHECK(!store.save(changed,&reason)&&reason.find("NEW opening.key")!=std::string::npos);
    set(member(changed,"opening"),"key",R"("new-demand-opening")");
    CHECK(store.save(changed,&reason));

    auto &opening=member(saved,"opening");
    set(opening,"through_utc_ms",std::to_string(middle));
    set(opening,"imported_kwh",R"("2.5")");
    set(opening,"accrued_subtotal",R"("1")");
    set(opening,"peak_demand_kw",R"("10")");
    CHECK(decodeTariffDeclaration(saved,&decoded)&&decoded.opening.accruedSubtotal.numerator.text()=="183600000000");
    checkComparison(compareTariffCycles(decoded,{dec("8000"),dec("12000")},1,T),"0.000000","10.000000","10.000000");
    panel.open(saved);
    enter(panel,"demand_charge");
    enter(panel,"window_minutes");type(panel,"5");
    const auto &after=*panel.draft.find("opening");
    CHECK(after.find("imported_kwh")->text=="2.5"&&after.find("accrued_subtotal")->text=="1");
    CHECK(after.find("peak_demand_kw")->text.empty());
    panel.key(27,{},T/1000);
    panel.key('d',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="1"&&!panel.draft.find("demand_charge"));
    CHECK(!panel.draft.find("opening")->find("peak_demand_kw"));
}

void exportPayment(JsonValue draft,const std::filesystem::path &target) {
    const auto close=T+3600000;
    auto &plan=member(draft,"plan");
    set(plan,"tiers",R"("none")");
    set(plan,"cycle_charge",R"("0")");
    set(plan,"subtotal_multiplier",R"("1")");
    auto &revision=member(plan,"revisions").items.front();
    set(revision,"end_utc_ms",std::to_string(close));
    set(revision,"time_mode",R"("constant")");
    set(revision,"base_rate_per_kwh",R"("3")");
    set(revision,"charge_per_civil_day",R"("0")");
    set(revision,"calendar","[]");
    set(revision,"bands","[]");
    set(member(draft,"opening"),"cycle_end_utc_ms",std::to_string(close));

    TariffSettingsPanel panel;
    panel.open(draft);
    enter(panel,"plan");
    panel.key(27,{},T/1000);
    CHECK(panel.render(120,25,ColorLevel::None).find("[E] Export payment")!=std::string::npos);
    panel.key('e',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="12");
    enter(panel,"export_payment");
    enter(panel,"rate_per_kwh");type(panel,"1");
    panel.key(27,{},T/1000);
    panel.key(27,{},T/1000);
    enter(panel,"opening");
    enter(panel,"exported_kwh");type(panel,"0");

    TariffDeclarationStore store(target);
    std::string reason;
    JsonValue saved;
    CHECK(store.load(&saved,&reason));
    panel.key('v',{},T/1000);
    CHECK(panel.render(120,25,ColorLevel::None).find("Export is paid separately")!=std::string::npos);
    panel.key('\r',[&](const JsonValue &value,std::string *error){return store.save(value,error);},T/1000);
    const bool persisted=store.load(&saved,&reason)&&saved.find("schema_version")->text=="12";
    CHECK(persisted);
    if(!persisted)return;

    TariffDeclaration decoded;
    const bool declared=decodeTariffDeclaration(saved,&decoded)&&decoded.plan.exportPayment.has_value();
    CHECK(declared);
    if(!declared)return;
    checkComparison(compareTariffCycles(decoded,{dec("-2000"),dec("-1000")},1,T),
                    "-2.000000","-1.000000","1.000000");
    checkComparison(compareTariffCycles(decoded,{dec("-1000"),dec("1000")},1,T),
                    "-1.000000","3.000000","4.000000");
    const PowerTrace absent{decoded.opening.evidence,{{static_cast<int64_t>(T),static_cast<int64_t>(close),dec("1000")}}};
    CHECK(evaluateBill(decoded.plan,decoded.opening,absent,close,T).status==ElectricityStatus::ExportEvidenceMissing);

    panel.key('p',{},T/1000);
    for(char c:std::string("-1000\t+1000\r"))panel.key(c,{},T/1000);
    const auto rendered=awaitComparison(panel);
    CHECK(rendered.find("Net without mining: -1.000000 USD")!=std::string::npos);
    CHECK(rendered.find("Net with mining:    3.000000 USD")!=std::string::npos);
    CHECK(rendered.find("Difference (with - without): 4.000000 USD")!=std::string::npos);
    frame(panel,60,12);

    for(const bool recurring:{false,true}){
        auto large=saved;
        auto &largePlan=member(large,"plan");
        auto &largeRate=member(largePlan,"revisions").items.front();
        const auto *rate=recurring?R"("1000000000000000000")":R"("1000000000000000000000")";
        set(largeRate,"base_rate_per_kwh",rate);
        set(member(largePlan,"export_payment"),"rate_per_kwh",rate);
        if(recurring){
            constexpr int64_t jan=1704067200000,feb=1706745600000,mar=1709251200000;
            set(large,"reset_policy",R"({"monthly_day":1,"clock":{"kind":"utc","fixed_offset_seconds":0},"daily_fee_allocation":"prorate_by_cycle_minutes"})");
            set(largeRate,"begin_utc_ms",std::to_string(jan));
            set(largeRate,"end_utc_ms",std::to_string(mar));
            auto &opening=member(large,"opening");
            set(opening,"cycle_begin_utc_ms",std::to_string(jan));
            set(opening,"cycle_end_utc_ms",std::to_string(feb));
            set(opening,"through_utc_ms",std::to_string(jan));
        }
        TariffSettingsPanel narrow;
        narrow.open(large);
        narrow.key('p',{},T/1000);
        if(recurring)narrow.key(kKeyRight,{},T/1000);
        for(char c:std::string("-100000000000000000\t+200000000000000000\r"))narrow.key(c,{},T/1000);
        const auto output=awaitComparison(narrow);
        CHECK(output.find("Net without mining:")!=std::string::npos);
        CHECK(output.find("Net with mining:")!=std::string::npos);
        CHECK(output.find("[Esc] Back")!=std::string::npos);
        if(recurring)CHECK(output.find("fees prorated")!=std::string::npos);
        frame(narrow,60,12);
    }

    auto changed=saved;
    set(member(member(changed,"plan"),"export_payment"),"rate_per_kwh",R"("0.5")");
    CHECK(!store.save(changed,&reason)&&reason.find("NEW plan.key")!=std::string::npos);
    set(member(changed,"plan"),"key",R"("new-export-plan")");
    CHECK(!store.save(changed,&reason)&&reason.find("NEW opening.key")!=std::string::npos);

    panel.open(saved);
    enter(panel,"plan");
    panel.key(27,{},T/1000);
    panel.key('e',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="1");
    CHECK(!panel.draft.find("plan")->find("export_payment"));
    CHECK(!panel.draft.find("opening")->find("exported_kwh"));
}

void apparentDemand(JsonValue draft,const std::filesystem::path &target) {
    const auto close=T+15*60000;
    auto &plan=member(draft,"plan");
    set(plan,"tiers",R"("none")");
    set(plan,"cycle_charge",R"("0")");
    set(plan,"subtotal_multiplier",R"("1")");
    auto &revision=member(plan,"revisions").items.front();
    set(revision,"end_utc_ms",std::to_string(close));
    set(revision,"time_mode",R"("constant")");
    set(revision,"base_rate_per_kwh",R"("1")");
    set(revision,"charge_per_civil_day",R"("0")");
    set(revision,"calendar","[]");
    set(revision,"bands","[]");
    set(member(draft,"opening"),"cycle_end_utc_ms",std::to_string(close));

    TariffSettingsPanel panel;
    panel.open(draft);
    enter(panel,"reset_policy");
    panel.key('d',{},T/1000);
    CHECK(panel.render(120,24,ColorLevel::None).find("[K] kW/kVA")!=std::string::npos);
    panel.key('k',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="11");
    CHECK(panel.draft.find("opening")->find("peak_demand_kva")->text.empty());
    CHECK(!panel.draft.find("opening")->find("peak_demand_kw"));
    enter(panel,"demand_charge");
    enter(panel,"window_minutes");type(panel,"15");
    enter(panel,"rate_per_kva");type(panel,"5");
    enter(panel,"power_factor_va_places");type(panel,"6");
    panel.key(27,{},T/1000);
    enter(panel,"opening");
    enter(panel,"peak_demand_kva");type(panel,"0");
    panel.key('v',{},T/1000);
    TariffDeclarationStore store(target);
    std::string reason;
    JsonValue saved;
    CHECK(store.load(&saved,&reason));
    panel.key('\r',[&](const JsonValue &value,std::string *error){return store.save(value,error);},T/1000);
    const bool persisted=store.load(&saved,&reason)&&saved.find("schema_version")->text=="11";
    CHECK(persisted);
    if(!persisted)return;
    TariffDeclaration decoded;
    const bool decodedApparent=decodeTariffDeclaration(saved,&decoded)&&decoded.plan.demandCharge&&decoded.plan.demandCharge->apparent();
    CHECK(decodedApparent);
    if(!decodedApparent)return;
    CHECK(decoded.plan.demandCharge->powerFactorVaPlaces==6);
    const std::array<ExactDecimal,2> watts{dec("0"),dec("8000")};
    CHECK(compareTariffCycles(decoded,watts,1,T).status==ElectricityStatus::DemandApparentEvidence);
    checkComparison(compareTariffCycles(decoded,watts,1,T,nullptr,{dec("0"),dec("10000")}),
                    "0.000000","52.000000","52.000000");

    TariffComparisonSettings comparison;
    comparison.open(saved);
    for(char c:std::string("0/0"))comparison.key(c,T/1000);
    comparison.key('\t',T/1000);
    for(char c:std::string("8000"))comparison.key(c,T/1000);
    comparison.key('\r',T/1000);
    CHECK(comparison.render(60,12,ColorLevel::None).find("both scenarios")!=std::string::npos);
    for(char c:std::string("@0.8"))comparison.key(c,T/1000);
    comparison.key('\r',T/1000);
    std::string text;
    for(unsigned attempt=0;attempt<200;++attempt){
        text=comparison.render(60,12,ColorLevel::None);
        if(text.find("52.000000 USD")!=std::string::npos)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(text.find("Remaining with mining:    52.000000 USD")!=std::string::npos);
    CHECK(text.find("Difference (with - without): 52.000000 USD")!=std::string::npos);

    panel.open(saved);
    enter(panel,"demand_charge");
    panel.key(27,{},T/1000);
    panel.key('k',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="9");
    CHECK(panel.draft.find("demand_charge")->find("rate_per_kw")->text.empty());
    CHECK(panel.draft.find("opening")->find("peak_demand_kw")->text.empty());
    CHECK(!panel.draft.find("demand_charge")->find("power_factor_va_places"));
}

void demandRatchet(JsonValue draft,const std::filesystem::path &target) {
    constexpr int64_t jan=1704067200000,feb=1706745600000,mar=1709251200000;
    auto &plan=member(draft,"plan");
    set(plan,"tiers",R"("none")");
    set(plan,"cycle_charge",R"("0")");
    set(plan,"subtotal_multiplier",R"("1")");
    auto &row=member(plan,"revisions").items.front();
    set(row,"begin_utc_ms",std::to_string(jan));
    set(row,"end_utc_ms",std::to_string(mar));
    set(row,"published_utc_ms",std::to_string(jan-1000));
    set(row,"time_mode",R"("constant")");
    set(row,"base_rate_per_kwh",R"("0")");
    set(row,"charge_per_civil_day",R"("0")");
    set(row,"calendar","[]");
    set(row,"bands","[]");
    set(draft,"schema_version","9");
    set(draft,"reset_policy",R"({"monthly_day":1,"clock":{"kind":"utc","fixed_offset_seconds":0}})");
    draft.members.emplace_back("demand_charge",json(R"({"window_minutes":15,"offset_minutes":0,"published_utc_ms":0,"rate_per_kw":"5"})"));
    auto &opening=member(draft,"opening");
    set(opening,"cycle_begin_utc_ms",std::to_string(jan));
    set(opening,"through_utc_ms",std::to_string(jan));
    set(opening,"cycle_end_utc_ms",std::to_string(feb));
    opening.members.emplace_back("peak_demand_kw",json(R"("0")"));

    TariffSettingsPanel panel;
    panel.open(draft);
    enter(panel,"demand_charge");
    panel.key(27,{},T/1000);
    CHECK(panel.render(120,26,ColorLevel::None).find("[R] Historical demand floor")!=std::string::npos);
    panel.key('r',{},T/1000);
    enter(panel,"ratchet");
    enter(panel,"lookback_months");type(panel,"1");
    enter(panel,"fraction");type(panel,"0.8");
    panel.key(27,{},T/1000);
    panel.key(27,{},T/1000);
    enter(panel,"opening");
    enter(panel,"demand_history");
    panel.key('a',{},T/1000);
    panel.key('\r',{},T/1000);
    enter(panel,"cycle_end_utc_ms");type(panel,std::to_string(jan));
    enter(panel,"peak_kw");type(panel,"20");
    panel.key('v',{},T/1000);
    CHECK(panel.render(120,27,ColorLevel::None).find("larger of this month's actual peak")!=std::string::npos);

    auto store=TariffDeclarationStore(target);
    std::string reason;
    JsonValue saved;
    CHECK(store.load(&saved,&reason));
    panel.key('\r',[&](const JsonValue &value,std::string *error){return store.save(value,error);},T/1000);
    CHECK(store.load(&saved,&reason)&&saved.find("schema_version")->text=="10");
    TariffDeclaration decoded;
    CHECK(decodeTariffDeclaration(saved,&decoded)&&decoded.plan.demandCharge->ratchet);
    CHECK(decoded.opening.accruedSubtotal.numerator.zero());
    const std::array<ExactDecimal,2> watts{dec("10000"),dec("24000")};
    CHECK(compareTariffCycles(decoded,watts,1,jan-1).status==ElectricityStatus::DemandHistoryNotYetKnown);
    checkComparison(compareTariffCycles(decoded,watts,1,jan),"80.000000","120.000000","40.000000");
    checkComparison(compareTariffCycles(decoded,watts,2,jan),"130.000000","240.000000","110.000000");

    auto &history=member(member(saved,"opening"),"demand_history");
    set(history.items.front(),"peak_kw",R"("21")");
    CHECK(!store.save(saved,&reason)&&reason.find("NEW opening.key")!=std::string::npos);
    set(member(saved,"opening"),"key",R"("new-ratchet-history")");
    CHECK(store.save(saved,&reason));

    panel.open(saved);
    enter(panel,"demand_charge");
    enter(panel,"window_minutes");type(panel,"5");
    CHECK(panel.draft.find("opening")->find("demand_history")->items.empty());
    panel.open(saved);
    enter(panel,"demand_charge");
    panel.key(27,{},T/1000);
    panel.key('r',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="9");
    CHECK(!panel.draft.find("opening")->find("demand_history"));
}

void seasonalCycles(JsonValue draft,const std::filesystem::path &target) {
    constexpr int64_t begin=1713657600000, middle=1714521600000, end=1716249600000, next=1718928000000;
    auto &plan=member(draft,"plan");
    auto &revision=member(plan,"revisions").items.front();
    set(plan,"tiers",R"("progressive")");
    set(plan,"cycle_charge",R"("0")");
    set(plan,"subtotal_multiplier",R"("1")");
    set(revision,"time_mode",R"("constant")");
    set(revision,"calendar","[]");
    set(revision,"base_rate_per_kwh",R"("0")");
    set(revision,"charge_per_civil_day",R"("0")");
    set(revision,"begin_utc_ms",std::to_string(begin));
    set(revision,"end_utc_ms",std::to_string(next));
    set(revision,"published_utc_ms",std::to_string(begin));
    set(revision,"bands",R"([{"through_kwh":null,"rate_per_kwh":"1"},{"through_kwh":null,"rate_per_kwh":"2"}])");
    auto &opening=member(draft,"opening");
    set(opening,"cycle_begin_utc_ms",std::to_string(begin));
    set(opening,"through_utc_ms",std::to_string(begin));
    set(opening,"cycle_end_utc_ms",std::to_string(end));
    set(opening,"imported_kwh",R"("0")");
    set(opening,"accrued_subtotal",R"("0")");
    set(draft,"schema_version","2");
    set(draft,"reset_policy",R"({"monthly_day":21,"clock":{"kind":"utc","fixed_offset_seconds":0}})");

    TariffSettingsPanel panel;
    panel.open(draft);
    enter(panel,"reset_policy");
    panel.key(27,{},T/1000);
    CHECK(panel.render(120,24,ColorLevel::None).find("[A] Seasonal allowance")!=std::string::npos);
    panel.key('a',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="7");
    enter(panel,"seasonal_allowance");
    enter(panel,"published_utc_ms");
    type(panel,std::to_string(begin-1000));
    enter(panel,"seasons");
    for(const auto &date:std::array<std::array<const char *,3>,2>{{{{"1","1","2"}},{{"5","1","3"}}}}){
        panel.key('a',{},T/1000);
        panel.key('\r',{},T/1000);
        enter(panel,"start_month");type(panel,date[0]);
        enter(panel,"start_day");type(panel,date[1]);
        enter(panel,"kwh_per_day");type(panel,date[2]);
        panel.key(27,{},T/1000);
    }
    panel.key('v',{},T/1000);
    CHECK(panel.render(120,28,ColorLevel::None).find("full cycle. Clear both band bounds")!=std::string::npos);
    JsonValue saved;
    panel.key('\r',[&](const JsonValue &candidate,std::string *){
        saved=candidate;
        return true;
    },T/1000);
    CHECK(saved.find("schema_version")&&saved.find("schema_version")->text=="7");
    CHECK(saved.find("seasonal_allowance")->find("seasons")->items.size()==2);
    CHECK(saved.find("plan")->find("revisions")->items.front().find("bands")->items.front().find("through_kwh")->type==JsonValue::Type::Null);
    TariffDeclaration decoded;
    TariffProblem problem;
    CHECK(decodeTariffDeclaration(saved,&decoded,&problem));
    std::optional<SeasonalCycleTariff> effective;
    CHECK(seasonalTariffForCycle(decoded,decoded.opening,T,effective)==ElectricityStatus::Ok);
    CHECK(effective&&effective->allowanceKwh.text()=="80");
    CHECK(seasonalTariffForCycle(decoded,decoded.opening,begin-1001,effective)==ElectricityStatus::NotYetKnown);
    CHECK(!effective);
    const std::array<ExactDecimal,2> watts{dec("100"),dec("200")};
    checkComparison(compareTariffCycles(decoded,watts,1,T),"72.000000","208.000000","136.000000");
    checkComparison(compareTariffCycles(decoded,watts,2,T),"146.400000","412.600000","266.200000");

    auto mid=saved;
    auto &midOpening=member(mid,"opening");
    set(midOpening,"through_utc_ms",std::to_string(middle));
    set(midOpening,"imported_kwh",R"("60")");
    set(midOpening,"accrued_subtotal",R"("60")");
    panel.open(mid);
    enter(panel,"seasonal_allowance");
    enter(panel,"seasons");
    panel.key('\r',{},T/1000);
    enter(panel,"kwh_per_day");type(panel,"1.5");
    CHECK(panel.draft.find("opening")->find("imported_kwh")->text=="60");
    CHECK(panel.draft.find("opening")->find("accrued_subtotal")->text.empty());
    CHECK(!decodeTariffDeclaration(panel.draft,&decoded,&problem));
    CHECK(panel.message.find("Re-enter the accrued subtotal")!=std::string::npos);

    auto explicitMid=mid;
    set(explicitMid,"reset_policy",R"("explicit_cycle_only")");
    auto &explicitOpening=member(explicitMid,"opening");
    set(explicitOpening,"imported_kwh",R"("90")");
    set(explicitOpening,"accrued_subtotal",R"("100")");
    CHECK(decodeTariffDeclaration(explicitMid,&decoded,&problem));
    panel.open(explicitMid);
    enter(panel,"opening");
    enter(panel,"cycle_end_utc_ms");type(panel,std::to_string(end-10*86400000LL));
    CHECK(panel.draft.find("opening")->find("imported_kwh")->text=="90");
    CHECK(panel.draft.find("opening")->find("accrued_subtotal")->text.empty());
    CHECK(!decodeTariffDeclaration(panel.draft,&decoded,&problem));
    enter(panel,"accrued_subtotal");type(panel,"130");
    CHECK(decodeTariffDeclaration(panel.draft,&decoded,&problem));
    CHECK(seasonalTariffForCycle(decoded,decoded.opening,T,effective)==ElectricityStatus::Ok);
    CHECK(effective&&effective->allowanceKwh.text()=="50");

    panel.open(explicitMid);
    enter(panel,"opening");
    enter(panel,"cycle_begin_utc_ms");type(panel,std::to_string(begin-86400000LL));
    CHECK(panel.draft.find("opening")->find("imported_kwh")->text.empty());
    CHECK(panel.draft.find("opening")->find("accrued_subtotal")->text.empty());
    panel.open(explicitMid);
    enter(panel,"opening");
    enter(panel,"through_utc_ms");type(panel,std::to_string(middle+86400000LL));
    CHECK(panel.draft.find("opening")->find("imported_kwh")->text.empty());
    CHECK(panel.draft.find("opening")->find("accrued_subtotal")->text.empty());

    auto fixed=explicitMid;
    set(member(member(fixed,"plan"),"clock"),"kind",R"("fixed_standard")");
    CHECK(decodeTariffDeclaration(fixed,&decoded,&problem));
    panel.open(fixed);
    enter(panel,"plan");
    enter(panel,"clock");
    enter(panel,"fixed_offset_seconds");type(panel,"3600");
    CHECK(panel.draft.find("opening")->find("imported_kwh")->text=="90");
    CHECK(panel.draft.find("opening")->find("accrued_subtotal")->text.empty());

    panel.open(saved);
    enter(panel,"reset_policy");
    panel.key(27,{},T/1000);
    panel.key('b',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="7");
    panel.key('a',{},T/1000);
    CHECK(!panel.draft.find("seasonal_allowance"));
    CHECK(panel.draft.find("schema_version")->text=="2");

    TariffDeclarationStore store(target);
    JsonValue loaded;
    std::string message;
    CHECK(store.load(&loaded,&message)&&store.save(saved,&message));
    auto revised=saved;
    auto &seasons=member(member(revised,"seasonal_allowance"),"seasons");
    set(seasons.items.front(),"kwh_per_day",R"("2.5")");
    CHECK(!store.save(revised,&message)&&message.find("NEW plan.key")!=std::string::npos);
    CHECK(store.load(&loaded,&message)&&tariffJson(loaded)==tariffJson(saved));
    set(member(revised,"plan"),"key",R"("revised-seasonal-plan")");
    CHECK(!store.save(revised,&message)&&message.find("NEW opening.key")!=std::string::npos);
    set(member(revised,"opening"),"key",R"("revised-seasonal-opening")");
    CHECK(store.save(revised,&message));
    CHECK(store.load(&loaded,&message)&&tariffJson(loaded)==tariffJson(revised));
}

void bimonthlyCycles(JsonValue draft) {
    constexpr int64_t jan2024=1704067200000, mar2024=1709251200000, may2024=1714521600000;
    constexpr int64_t dec2023=1703980800000, leap2024=1709164800000, april2024=1714435200000;
    const auto even=json(R"({"bimonthly_start_month":2,"bimonthly_day":31,"clock":{"kind":"utc","fixed_offset_seconds":0}})");
    TariffCycleBounds bounds;
    TariffProblem error;
    CHECK(deriveTariffRecurringCycle(even,json(std::to_string(leap2024-1)),bounds,&error));
    CHECK(bounds.beginUtcMs==dec2023&&bounds.endUtcMs==leap2024);
    CHECK(deriveTariffRecurringCycle(even,json(std::to_string(leap2024)),bounds,&error));
    CHECK(bounds.beginUtcMs==leap2024&&bounds.endUtcMs==april2024);
    CHECK(!deriveTariffMonthlyCycle(even,json(std::to_string(leap2024)),bounds,&error));
    CHECK(bounds.beginUtcMs==leap2024&&bounds.endUtcMs==april2024);

    auto &plan=member(draft,"plan");
    auto &rate=member(plan,"revisions").items.front();
    set(plan,"tiers",R"("all_units")");
    set(plan,"cycle_charge",R"("0")");
    set(plan,"minimum_cycle_subtotal","null");
    set(plan,"subtotal_multiplier",R"("1")");
    set(rate,"begin_utc_ms",std::to_string(jan2024));
    set(rate,"published_utc_ms",std::to_string(jan2024));
    set(rate,"end_utc_ms",std::to_string(may2024));
    set(rate,"time_mode",R"("constant")");
    set(rate,"calendar","[]");
    set(rate,"base_rate_per_kwh",R"("0")");
    set(rate,"charge_per_civil_day",R"("0")");
    set(rate,"bands",R"([{"through_kwh":"100","rate_per_kwh":"1"},{"through_kwh":null,"rate_per_kwh":"2"}])");
    set(member(draft,"opening"),"through_utc_ms",std::to_string(jan2024));
    TariffSettingsPanel panel;
    panel.open(draft);
    enter(panel,"reset_policy");
    CHECK(panel.render(90,20,ColorLevel::None).find("[B] Two-month reset")!=std::string::npos);
    panel.key('b',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="6");
    CHECK(panel.draft.find("opening")->find("imported_kwh")->text.empty());
    CHECK(panel.draft.find("opening")->find("accrued_subtotal")->text.empty());
    panel.key('f',{},T/1000);
    CHECK(!panel.draft.find("reset_policy")->find("daily_fee_allocation"));
    enter(panel,"reset_policy");
    enter(panel,"bimonthly_start_month");
    type(panel,"1");
    enter(panel,"bimonthly_day");
    type(panel,"1");
    panel.key('g',{},T/1000);
    CHECK(panel.draft.find("opening")->find("cycle_begin_utc_ms")->text==std::to_string(jan2024));
    CHECK(panel.draft.find("opening")->find("cycle_end_utc_ms")->text==std::to_string(mar2024));
    enter(panel,"imported_kwh");type(panel,"0");
    enter(panel,"accrued_subtotal");type(panel,"0");
    panel.key('v',{},T/1000);
    CHECK(panel.render(110,30,ColorLevel::None).find("monthly-average usage")!=std::string::npos);
    JsonValue saved;
    panel.key('\r',[&](const JsonValue &candidate,std::string *){
        saved=candidate;
        return true;
    },T/1000);
    CHECK(saved.find("schema_version")&&saved.find("schema_version")->text=="6");
    CHECK(saved.find("plan")->find("revisions")->items.front().find("bands")->items.front().find("through_kwh")->text=="100");
    TariffDeclaration decoded;
    CHECK(decodeTariffDeclaration(saved,&decoded,&error));
    CHECK(decoded.plan.revisions.front().bands.front().throughKwh->text()=="200");
    CHECK(tariffComparisonEnd(decoded,2)==may2024);
    checkComparison(compareTariffCycles(decoded,{dec("100"),dec("200")},1,T),"144.000000","576.000000","432.000000");

    panel.open(saved);
    enter(panel,"plan");
    enter(panel,"minimum_cycle_subtotal");
    const auto minimumHint=panel.render(120,24,ColorLevel::None);
    CHECK(minimumHint.find("monthly value")!=std::string::npos);
    CHECK(minimumHint.find("clear for null")!=std::string::npos);
    frame(panel,60,12);
    panel.key(27,{},T/1000);
    enter(panel,"revisions");
    panel.key('\r',{},T/1000);
    enter(panel,"bands");
    panel.key(kKeyDown,{},T/1000);
    panel.key('\r',{},T/1000);
    enter(panel,"through_kwh");
    const auto bandHint=panel.render(120,24,ColorLevel::None);
    CHECK(bandHint.find("monthly value")!=std::string::npos);
    CHECK(bandHint.find("Final band MUST be unbounded/null")!=std::string::npos);
    frame(panel,60,12);

    panel.open(saved);
    enter(panel,"reset_policy");
    panel.key(27,{},T/1000);
    panel.key('m',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="2");
    CHECK(panel.draft.find("reset_policy")->find("monthly_day")->text=="1");
    CHECK(panel.draft.find("opening")->find("accrued_subtotal")->text.empty());
}

void monthlyCycles(const JsonValue &original) {
    using Message=i18n::TariffMonthlyMessageKey;
    for(size_t i=0;i<static_cast<size_t>(Message::Count);++i) {
        const auto key=static_cast<Message>(i);
        const auto source=i18n::tariffMonthlyMessage("en",key);
        const auto fallback=i18n::tariffMonthlyMessage("zh-Hans",key);
        CHECK(!source.text.empty()&&!source.fallback&&source.review==i18n::ReviewStatus::Source);
        CHECK(fallback.text==source.text&&fallback.fallback&&fallback.locale=="en");
    }
    CHECK(i18n::tariffMonthlyMessage("en",Message::Count).text.empty());
    constexpr int64_t jan2023=1675123200000, feb2023=1677542400000, mar2023=1680220800000;
    constexpr int64_t jan2024=1706659200000, feb2024=1709164800000, mar2024=1711843200000;
    const auto utc=json(R"({"monthly_day":31,"clock":{"kind":"utc","fixed_offset_seconds":0}})");
    TariffCycleBounds bounds;
    TariffProblem error;
    const auto check=[&](const JsonValue &policy,int64_t through,int64_t begin,int64_t end){
        CHECK(deriveTariffMonthlyCycle(policy,json(std::to_string(through)),bounds,&error));
        CHECK(error.path.empty()&&bounds.beginUtcMs==begin&&bounds.endUtcMs==end);
    };
    check(utc,feb2023-1,jan2023,feb2023);
    check(utc,feb2023,feb2023,mar2023);
    check(utc,feb2024-1,jan2024,feb2024);
    check(utc,feb2024,feb2024,mar2024);
    auto fixed=utc;
    set(member(fixed,"clock"),"kind","\"fixed_standard\"");
    set(member(fixed,"clock"),"fixed_offset_seconds","19800");
    check(fixed,feb2024-19800001,jan2024-19800000,feb2024-19800000);
    check(fixed,feb2024-19800000,feb2024-19800000,mar2024-19800000);
    set(fixed,"monthly_day","1");
    set(member(fixed,"clock"),"fixed_offset_seconds","-12600");
    check(fixed,1735689600000+12600000,1735689600000+12600000,1738368000000+12600000);
    const auto prior=bounds;
    for(const auto &instant:std::vector<std::string>{"0","253402300799999","253402300800000","true","\"1709164800000\""}) {
        CHECK(!deriveTariffMonthlyCycle(utc,json(instant),bounds,&error)&&error.path=="opening.through_utc_ms");
        CHECK(bounds.beginUtcMs==prior.beginUtcMs&&bounds.endUtcMs==prior.endUtcMs);
    }
    for(const auto &day:std::vector<std::string>{"0","32","true","31.0","\"31\""}) {
        auto policy=utc;
        set(policy,"monthly_day",day);
        CHECK(!deriveTariffMonthlyCycle(policy,json(std::to_string(feb2024)),bounds,&error)&&error.path=="reset_policy.monthly_day");
    }
    auto named=utc;
    set(member(named,"clock"),"kind","\"named_iana\"");
    CHECK(!deriveTariffMonthlyCycle(named,json(std::to_string(feb2024)),bounds,&error)&&error.path=="reset_policy.clock");

    auto v=original;
    auto &plan=member(v,"plan");
    auto &opening=member(v,"opening");
    auto &revision=member(plan,"revisions").items[0];
    set(plan,"tiers","\"none\"");
    set(revision,"time_mode","\"constant\"");
    set(revision,"charge_per_civil_day","\"0\"");
    set(revision,"calendar","[]");
    set(revision,"bands","[]");
    set(revision,"begin_utc_ms",std::to_string(jan2024));
    set(revision,"end_utc_ms",std::to_string(mar2024));
    set(revision,"published_utc_ms","1700000000000");
    set(opening,"cycle_begin_utc_ms",std::to_string(jan2024));
    set(opening,"cycle_end_utc_ms",std::to_string(mar2024));
    set(opening,"through_utc_ms","1711756800000"); // March 30 at 00:00 UTC
    set(opening,"imported_kwh","\"24\"");
    set(opening,"accrued_subtotal","\"2\"");
    TariffDeclaration decoded;
    CHECK(decodeTariffDeclaration(v,&decoded,&error));
    TariffSettingsPanel panel;
    panel.open(v);
    enter(panel,"reset_policy");
    CHECK(panel.render(60,12,ColorLevel::None).find("[M] Cycle policy")!=std::string::npos);
    panel.key('m',{},T/1000);
    frame(panel,60,12);
    panel.key('g',{},T/1000);
    CHECK(panel.message.find("reset_policy.monthly_day")!=std::string::npos);
    enter(panel,"monthly_day");
    type(panel,"31");
    const auto before=tariffJson(panel.draft);
    panel.key('g',{},T/1000);
    CHECK(tariffJson(panel.draft)==before&&panel.path()=="opening"&&panel.message.find("Clear both")!=std::string::npos);
    enter(panel,"cycle_begin_utc_ms");
    CHECK(panel.page==TariffSettingsPage::Browse);
    enter(panel,"imported_kwh");
    panel.key(127,{},T/1000);
    type(panel,"");
    panel.key('g',{},T/1000);
    CHECK(panel.message.find("Clear both")!=std::string::npos);
    enter(panel,"accrued_subtotal");
    panel.key(127,{},T/1000);
    type(panel,"");
    panel.key('g',{},T/1000);
    CHECK(panel.message.find("Draft cycle dates derived")!=std::string::npos);
    CHECK(panel.draft.find("opening")->find("cycle_begin_utc_ms")->text==std::to_string(feb2024));
    CHECK(panel.draft.find("opening")->find("cycle_end_utc_ms")->text==std::to_string(mar2024));
    CHECK(panel.draft.find("opening")->find("through_utc_ms")->text=="1711756800000");
    enter(panel,"imported_kwh");type(panel,"21");
    enter(panel,"accrued_subtotal");type(panel,"2");
    panel.key('v',{},T/1000);
    const auto review=panel.render(120,100,ColorLevel::None);
    CHECK(review.find("Monthly reset at 00:00")!=std::string::npos);
    CHECK(review.find("reset_policy: \"explicit_cycle_only\" -> {")!=std::string::npos);
    auto mismatched=panel.draft;
    set(member(mismatched,"opening"),"cycle_end_utc_ms",std::to_string(mar2024-1));
    CHECK(!decodeTariffDeclaration(mismatched,&decoded,&error)&&error.path=="opening");
    CHECK(decoded.opening.cycleBeginUtcMs==jan2024);
    CHECK(decodeTariffDeclaration(panel.draft,&decoded,&error));
    CHECK(decoded.opening.cycleBeginUtcMs==feb2024&&decoded.opening.cycleEndUtcMs==mar2024);
    auto atReset=panel.draft;
    auto &resetOpening=member(atReset,"opening");
    set(resetOpening,"through_utc_ms",std::to_string(feb2024));
    set(resetOpening,"imported_kwh","\"0\"");
    set(resetOpening,"accrued_subtotal","\"0\"");
    TariffSettingsPanel boundary;
    boundary.open(atReset);
    enter(boundary,"reset_policy");
    boundary.key(27,{},T/1000);
    boundary.key('f',{},T/1000);
    CHECK(boundary.draft.find("opening")->find("accrued_subtotal")->text=="0");
    CHECK(decodeTariffDeclaration(boundary.draft,&decoded,&error));
    comparisonHorizon(panel.draft);
    panel.open(panel.draft);
    enter(panel,"reset_policy");
    panel.key(27,{},T/1000);
    CHECK(panel.render(85,18,ColorLevel::None).find("[F] Daily fee rule")!=std::string::npos);
    panel.key('f',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="3"&&panel.active.find("schema_version")->text=="2");
    CHECK(panel.draft.find("opening")->find("accrued_subtotal")->text.empty());
    CHECK(panel.active.find("opening")->find("accrued_subtotal")->text=="2");
    CHECK(!decodeTariffDeclaration(panel.draft,&decoded,&error)&&error.path=="opening.accrued_subtotal");
    enter(panel,"opening");
    enter(panel,"accrued_subtotal");type(panel,"2");
    panel.key(27,{},T/1000);
    CHECK(decodeTariffDeclaration(panel.draft,&decoded,&error));
    CHECK(decoded.dailyFeeAllocation()==DailyFeeAllocation::ProrateByCycleMinutes);
    auto opted=panel.draft;
    set(opted,"schema_version","2");
    CHECK(!decodeTariffDeclaration(opted,&decoded,&error)&&error.path=="reset_policy.daily_fee_allocation");
    panel.key('v',{},T/1000);
    CHECK(panel.render(120,100,ColorLevel::None).find("split by minutes in each billing cycle")!=std::string::npos);
    panel.open(panel.draft);
    enter(panel,"reset_policy");
    panel.key(27,{},T/1000);
    panel.key('f',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="2"&&!panel.draft.find("reset_policy")->find("daily_fee_allocation"));
    CHECK(panel.draft.find("opening")->find("accrued_subtotal")->text.empty());
    panel.key('f',{},T/1000);
    enter(panel,"opening");
    enter(panel,"accrued_subtotal");type(panel,"2");
    panel.key(27,{},T/1000);
    enter(panel,"reset_policy");
    panel.key(27,{},T/1000);
    panel.key('m',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="1"&&panel.draft.find("opening")->find("accrued_subtotal")->text.empty());
    panel.draft=panel.active;
    panel.key('f',{},T/1000);
    enter(panel,"opening");
    enter(panel,"accrued_subtotal");type(panel,"2");
    panel.key(27,{},T/1000);
    const auto changes=tariffDeclarationChanges(panel.active,panel.draft);
    CHECK(std::any_of(changes.begin(),changes.end(),[](const auto &change){
        return change.find("reset_policy.daily_fee_allocation:")!=std::string::npos;
    }));
    panel.open(panel.draft);
    panel.key('p',{},T/1000);
    for(char c:std::string("0\t1000\r"))panel.key(c,{},T/1000);
    CHECK(awaitComparison(panel).find("5.280000 USD")!=std::string::npos);
    panel.key(27,{},T/1000);
    enter(panel,"reset_policy");
    CHECK(panel.path()=="reset_policy");
    panel.key(27,{},T/1000);
    panel.key('m',{},T/1000);
    CHECK(panel.draft.find("schema_version")->text=="1");
    CHECK(panel.draft.find("reset_policy")->text=="explicit_cycle_only");
    CHECK(decodeTariffDeclaration(panel.draft,&decoded,&error));
}
}
int main(int argc,char **argv){
    auto original=fixture();TariffDeclaration d;TariffProblem error;CHECK(decodeTariffDeclaration(original,&d,&error));
    monthlyCycles(original);
    annualCycles(original);
    bimonthlyCycles(original);
    CHECK(d.plan.tiers==TierMode::Progressive&&d.plan.revisions[0].timeMode==TimeRateMode::Calendar&&d.opening.evidence.origin==PowerOrigin::DeclaredForecast);
    CHECK(d.opening.planKey==d.plan.key&&d.sourceReference.find("SYNTHETIC")!=std::string::npos);
    PowerTrace trace{d.opening.evidence,{{static_cast<int64_t>(T),static_cast<int64_t>(T+86400000),dec("1000")}}};
    const auto bill=evaluateBill(d.plan,d.opening,trace,T+86400000,T);std::string text;
    CHECK(bill.status==ElectricityStatus::Ok&&bill.charge&&bill.charge->format(2,&text)&&text=="11.44");
    CHECK(bill.ending&&bill.ending->importedKwh.format(2,&text)&&text=="24.00");
    // Continuation from the same immutable opening must not charge the cycle/day twice.
    const auto split=T+3600000;
    PowerTrace first{d.opening.evidence,{{static_cast<int64_t>(T),static_cast<int64_t>(split),dec("1000")}}};
    auto partial=evaluateBill(d.plan,d.opening,first,split,T);CHECK(partial.status==ElectricityStatus::Ok);
    PowerTrace rest{d.opening.evidence,{{static_cast<int64_t>(split),static_cast<int64_t>(T+86400000),dec("1000")}}};
    auto remainder=evaluateBill(d.plan,*partial.ending,rest,T+86400000,T);ExactDecimal sum;
    CHECK(remainder.status==ElectricityStatus::Ok&&partial.charge->numerator.add(remainder.charge->numerator,&sum)&&sum.compare(bill.charge->numerator)==0);
    JsonValue round;CHECK(parseTariffDeclaration(tariffJson(original),&round,&error)&&tariffJson(round)==tariffJson(original));
    const auto fullReview=tariffDeclarationChanges({},original);
    CHECK(std::any_of(fullReview.begin(),fullReview.end(),[](const std::string &line){return line.find("plan.revisions[0].calendar[0].rate_per_kwh: ")==0;}));
    CHECK(std::any_of(fullReview.begin(),fullReview.end(),[](const std::string &line){return line.find("plan.revisions[0].bands[1].rate_per_kwh: ")==0;}));
    for(const auto &bad:std::vector<std::string>{"{}","[]",tariffJson(original)+"x",std::string(tariffDeclarationMaxBytes+1,' '),"{\"schema_version\":1,\"schema_version\":1}"}){
        const auto before=tariffJson(round);CHECK(!parseTariffDeclaration(bad,&round,&error)&&tariffJson(round)==before);
    }
    auto reject=[&](const JsonValue &v){TariffDeclaration old=d;CHECK(!decodeTariffDeclaration(v,&old,&error)&&old.plan.key==d.plan.key&&!error.message.empty());};
    reject(tariffDeclarationTemplate());
    for(const auto &bad:std::vector<std::pair<const char *,std::string>>{{"schema_version","2"},{"acknowledged","false"},{"confirmed_utc","0"},{"confirmed_utc","253402300800"},{"reset_policy","\"monthly_automatic\""},{"country","\"bad\\u001b[2J\""}}){auto v=original;set(v,bad.first,bad.second);reject(v);}
    for(const auto &bad:std::vector<std::pair<const char *,std::string>>{{"cycle_charge","\"\""},{"cycle_charge","0"},{"cycle_charge","\"1e2\""},{"cycle_charge","\"-0\""},{"cycle_charge","\"0.50\""},{"cycle_charge","\"0.0000000000000000001\""},{"currency","\"USDT\""},{"rounding","\"per_component\""},{"subtotal_multiplier","\"-1\""},{"minimum_cycle_subtotal","\"-1\""},{"tiers","\"independent_tou_tiers\""}}){auto v=original;set(member(v,"plan"),bad.first,bad.second);reject(v);}
    for(const auto &bad:std::vector<std::pair<const char *,std::string>>{{"basis","\"gpu_board\""},{"origin","\"observed_telemetry\""},{"time_database_version","\"OS-current-zone\""},{"cycle_end_utc_ms",std::to_string(T+371*86400000ULL)},{"through_utc_ms",std::to_string(T-1)},{"through_utc_ms",std::to_string(T+86400000)},{"imported_kwh","\"-1\""},{"imported_kwh","\"1\""},{"accrued_subtotal","\"1\""}}){auto v=original;set(member(v,"opening"),bad.first,bad.second);reject(v);}
    for(const auto &entry:std::vector<std::string>{"{\"kind\":\"utc\",\"fixed_offset_seconds\":60}","{\"kind\":\"fixed_standard\",\"fixed_offset_seconds\":1}","{\"kind\":\"fixed_standard\",\"fixed_offset_seconds\":-0}","{\"kind\":\"named_iana\",\"fixed_offset_seconds\":0}"}){auto v=original;set(member(v,"plan"),"clock",entry);reject(v);}
    auto v=original;set(member(v,"plan"),"clock","{\"kind\":\"fixed_standard\",\"fixed_offset_seconds\":-3600}");CHECK(decodeTariffDeclaration(v,&d,&error));
    v=original;member(v,"plan").members.emplace_back("demand_charge",json("\"0\""));reject(v);
    v=original;member(v,"opening").members.pop_back();reject(v);
    v=original;auto &revisions=member(member(v,"plan"),"revisions").items;revisions.push_back(revisions.front());reject(v);
    v=original;set(member(member(v,"plan"),"revisions").items[0],"time_mode","\"constant\"");reject(v); // stale calendar not silently dropped
    v=original;set(member(member(v,"plan"),"revisions").items[0],"published_utc_ms",std::to_string(T+1));reject(v);
    v=original;set(member(member(member(v,"plan"),"revisions").items[0],"calendar").items[0],"date","\"2026-02-30\"");reject(v);
    v=original;auto &bands=member(member(member(v,"plan"),"revisions").items[0],"bands").items;set(bands.back(),"through_kwh","\"20\"");reject(v);
    v=original;auto &many=member(member(member(v,"plan"),"revisions").items[0],"calendar").items;many.resize(33,many.front());reject(v);
    // Signed dynamic records + tier composition, strict no carry-forward, no silent overlap repair.
    v=original;auto &r=member(member(v,"plan"),"revisions").items[0];set(r,"time_mode","\"dynamic\"");set(r,"base_rate_per_kwh","\"0\"");set(r,"calendar","[]");
    set(r,"dynamic","[{\"begin_utc_ms\":1800000000000,\"end_utc_ms\":1800086400000,\"published_utc_ms\":1799999999000,\"rate_per_kwh\":\"-0.1\"}]");
    CHECK(decodeTariffDeclaration(v,&d,&error));auto dynamic=evaluateBill(d.plan,d.opening,trace,T+86400000,T);CHECK(dynamic.status==ElectricityStatus::Ok&&dynamic.charge->format(2,&text)&&text=="2.64");
    auto gap=v;set(member(member(member(gap,"plan"),"revisions").items[0],"dynamic").items[0],"end_utc_ms",std::to_string(T+1000));
    CHECK(decodeTariffDeclaration(gap,&d,&error));CHECK(evaluateBill(d.plan,d.opening,trace,T+86400000,T).status==ElectricityStatus::MissingPrice);
    auto overlap=v;auto &records=member(member(member(overlap,"plan"),"revisions").items[0],"dynamic").items;records.push_back(records.front());reject(overlap);
    v=original;set(member(v,"plan"),"tiers","\"all_units\"");CHECK(decodeTariffDeclaration(v,&d,&error));
    CHECK(evaluateBill(d.plan,d.opening,first,split,T).status==ElectricityStatus::Unsupported);
    CHECK(evaluateBill(d.plan,d.opening,trace,T+86400000,T).charge->format(2,&text)&&text=="12.54");
    // Load/save/CAS/version binding and failed loads across independent store instances.
    const auto root=std::filesystem::path(makeScratchDir("tariff-declaration")),file=root/"tariff-declaration.json";
    seasonalCycles(original,root/"seasonal-version-test.json");
    componentMultipliers(original,root/"component-version-test.json");
    demandWindows(original,root/"demand-version-test.json");
    demandRatchet(original,root/"ratchet-version-test.json");
    apparentDemand(original,root/"apparent-demand-version-test.json");
    exportPayment(original,root/"export-payment-version-test.json");
    TariffDeclarationStore store(file),other(file);JsonValue loaded;
    std::string message;CHECK(!store.save(original,&message));CHECK(store.load(&loaded,&message)&&!std::filesystem::exists(file));
    CHECK(!store.save(loaded,&message)&&!std::filesystem::exists(file));CHECK(store.save(original,&message)&&other.load(&loaded,&message));
    v=original;set(member(v,"plan"),"cycle_charge","\"3\"");CHECK(!store.save(v,&message)&&message.find("NEW plan.key")!=std::string::npos);
    set(member(v,"plan"),"key","\"synthetic-v2\"");CHECK(!store.save(v,&message)&&message.find("NEW opening.key")!=std::string::npos);
    set(member(v,"opening"),"key","\"synthetic-opening-v2\"");CHECK(store.save(v,&message)&&!other.save(original,&message));
    CHECK(other.load(&loaded,&message)&&tariffJson(loaded)==tariffJson(v));
    {auto lock=file;lock+=".lock";RuntimePreferencesLease lease;CHECK(lease.acquire(lock)&&!other.save(loaded,&message));}
    {std::ofstream out(file);out<<"{\"schema_version\":99}";}
    const auto before=tariffJson(loaded);CHECK(!other.load(&loaded,&message)&&tariffJson(loaded)==before&&!other.save(original,&message));
    std::filesystem::remove(file);std::filesystem::create_directory(file);CHECK(!other.load(&loaded,&message));std::filesystem::remove_all(file);
    CHECK(store.load(&loaded,&message));
    // Actual state machine: nested editing, review, failed save, exact publication and no other draft save.
    TariffSettingsPanel panel;panel.open(original);enter(panel,"plan");enter(panel,"key");CHECK(panel.page==TariffSettingsPage::Edit);type(panel,"synthetic-v3");
    panel.key(27,{},T/1000);enter(panel,"opening");enter(panel,"key");type(panel,"synthetic-opening-v3");panel.key(27,{},T/1000);
    CHECK(panel.dirty());panel.key('v',{},T/1000);CHECK(panel.page==TariffSettingsPage::Review);
    CHECK(panel.render(120,100,ColorLevel::None).find("EXCLUDES demand/export")!=std::string::npos);
    unsigned saves=0;auto save=[&](const JsonValue &value,std::string *e){++saves;return store.save(value,e);};
    panel.key('\r',save,T/1000);CHECK(saves==1&&!panel.dirty()&&panel.message.find("Saved offline")!=std::string::npos);
    CHECK(other.load(&loaded,&message)&&member(member(loaded,"plan"),"key").text=="synthetic-v3");
    enter(panel,"plan");enter(panel,"cycle_charge");type(panel,"4");panel.key('v',save,T/1000);panel.key('\r',save,T/1000);CHECK(panel.dirty()&&panel.message.find("NEW plan.key")!=std::string::npos);
    panel.key(27,save,T/1000);panel.key(27,save,T/1000);panel.key(27,save,T/1000);CHECK(panel.page==TariffSettingsPage::Leave);panel.key('d',save,T/1000);CHECK(!panel.visible&&!panel.dirty());
    panel.open(original);enter(panel,"country");for(char c:std::string("QDSL"))panel.key(c,{},T/1000);CHECK(panel.visible&&panel.input=="QDSL");CHECK(panel.key(3,{},T/1000)==TariffSettingsResult::Quit);panel.key(27,{},T/1000);CHECK(!panel.dirty());
    enter(panel,"country");for(unsigned i=0;i<129;++i)panel.key('a',{},T/1000);panel.key('\r',{},T/1000);CHECK(panel.inputRejected&&panel.page==TariffSettingsPage::Edit&&!panel.dirty());panel.key(27,{},T/1000);
    enter(panel,"country");panel.key(0xc3,{},T/1000);panel.key('\r',{},T/1000);CHECK(panel.inputRejected&&!panel.dirty());panel.key(27,{},T/1000);
    panel.open(original);enter(panel,"plan");enter(panel,"revisions");panel.key('a',{},T/1000);CHECK(panel.draft.find("plan")->find("revisions")->items.size()==2);panel.key('x',{},T/1000);panel.key(27,{},T/1000);CHECK(panel.draft.find("plan")->find("revisions")->items.size()==2);panel.key('x',{},T/1000);panel.key('\r',{},T/1000);CHECK(!panel.dirty());
    panel.open(tariffDeclarationTemplate());panel.key('v',{},T/1000);panel.key('\r',save,T/1000);CHECK(panel.page==TariffSettingsPage::Browse&&panel.message.find("acknowledged")!=std::string::npos&&panel.field==3);
    // The comparison uses the saved plan, even when a new draft has different fees.
    panel.open(original);
    set(member(panel.draft,"plan"),"cycle_charge","\"9\"");
    const auto savedDraft=tariffJson(panel.draft);
    panel.key('p',save,T/1000);
    CHECK(panel.render(60,12,ColorLevel::None).find("HYPOTHETICAL")!=std::string::npos);
    panel.key('\r',save,T/1000);
    CHECK(panel.render(60,12,ColorLevel::None).find("Enter both watt values")!=std::string::npos);
    for(char c:std::string("0\t1000\r"))panel.key(c,save,T/1000);
    auto comparison=awaitComparison(panel);
    CHECK(comparison.find("2.420000 USD")!=std::string::npos&&comparison.find("11.440000 USD")!=std::string::npos&&comparison.find("9.020000 USD")!=std::string::npos);
    frame(panel,60,12);
    panel.key('x',save,T/1000);
    CHECK(panel.render(60,12,ColorLevel::None).find("9.020000 USD")==std::string::npos);
    panel.key('\r',save,T/1000);
    CHECK(panel.render(60,12,ColorLevel::None).find("Enter both watt values")!=std::string::npos);
    panel.key(127,save,T/1000);
    panel.key('\r',save,T/1000-1);
    CHECK(panel.render(60,12,ColorLevel::None).find("current local time")!=std::string::npos);
    CHECK(panel.key(3,save,T/1000)==TariffSettingsResult::Quit);
    panel.key(27,save,T/1000);
    CHECK(tariffJson(panel.draft)==savedDraft&&saves==2);
    panel.open(gap);
    panel.key('p',{},T/1000);
    for(char c:std::string("0\t1000\r"))panel.key(c,{},T/1000);
    comparison=awaitComparison(panel);
    CHECK(comparison.find("Comparison unavailable: ")!=std::string::npos&&comparison.find("Remaining without mining: ")==std::string::npos);
    auto allUnits = original;
    set(member(allUnits,"plan"),"tiers","\"all_units\"");
    auto &opening = member(allUnits,"opening");
    set(opening,"through_utc_ms",std::to_string(T+43200000));
    set(opening,"imported_kwh","\"9\"");
    set(opening,"accrued_subtotal","\"3\"");
    panel.open(allUnits);
    panel.key('p',{},T/1000);
    for(char c:std::string("0\t1000\r"))panel.key(c,{},T/1000);
    comparison=awaitComparison(panel);
    CHECK(comparison.find("0.110000 USD")!=std::string::npos&&comparison.find("6.160000 USD")!=std::string::npos&&comparison.find("6.050000 USD")!=std::string::npos);
    panel.key('\r',{},T/1000);
    CHECK(panel.key('q',{},T/1000)==TariffSettingsResult::Quit);
    CHECK(awaitComparison(panel).find("6.050000 USD")==std::string::npos);
    panel.key(27,{},T/1000);
    CHECK(panel.render(60,12,ColorLevel::None).find("HYPOTHETICAL")==std::string::npos);
    panel.key('p',{},T/1000);
    const auto reopened=panel.render(60,12,ColorLevel::None);
    CHECK(reopened.find("Without mining (W): \n")!=std::string::npos);
    CHECK(reopened.find("6.050000 USD")==std::string::npos);
    panel.key(27,{},T/1000);
    auto large=original;
    auto &plan=member(large,"plan");
    set(plan,"tiers","\"none\"");
    set(plan,"cycle_charge","\"0\"");
    set(plan,"subtotal_multiplier","\"1\"");
    auto &revision=member(plan,"revisions").items[0];
    set(revision,"time_mode","\"constant\"");
    set(revision,"calendar","[]");
    set(revision,"bands","[]");
    set(revision,"base_rate_per_kwh","\"100000000000\"");
    set(revision,"charge_per_civil_day","\"0\"");
    // The cancellation scenario may still own its worker slot while it retires.
    TariffSettingsPanel largePanel;
    largePanel.open(large);
    largePanel.key('p',{},T/1000);
    for(char c:std::string("100000000000000000\t200000000000000000\r"))largePanel.key(c,{},T/1000);
    const auto largeComparison=awaitComparison(largePanel);
    CHECK(largeComparison.find("Remaining without mining:")!=std::string::npos);
    CHECK(largeComparison.find("Remaining with mining:")!=std::string::npos);
    CHECK(largeComparison.find("Difference (with - without):")!=std::string::npos);
    CHECK(largeComparison.find("240000000000000000000000000.000000")!=std::string::npos);
    CHECK(largeComparison.find("480000000000000000000000000.000000")!=std::string::npos);
    CHECK(std::count(largeComparison.begin(),largeComparison.end(),'\n')==11&&largeComparison.back()!='\n');
    CHECK(largeComparison.find("HYPOTHETICAL")<largeComparison.find("[Tab] Field"));
    frame(largePanel,60,12);
    const std::atomic<bool> cancelled{true};
    CHECK(counterfactualBill(d.plan,d.opening,trace,trace,T+86400000,T,&cancelled).status==ElectricityStatus::Unavailable);
    panel.open(tariffDeclarationTemplate());
    panel.key('p',{},T/1000);
    CHECK(panel.render(60,12,ColorLevel::None).find("Save a valid combined tariff")!=std::string::npos);
    // Focus remains visible at the supported compact terminal sizes, including deep table routes.
    panel.open(original);enter(panel,"plan");enter(panel,"revisions");panel.key('\r',{},T/1000);enter(panel,"calendar");panel.key('\r',{},T/1000);
    for(int columns:{60,80,120})for(int rows:{12,24})for(size_t i=0;i<9;++i){panel.field=i;frame(panel,columns,rows);CHECK(panel.render(columns,rows,ColorLevel::None).find("> ")!=std::string::npos);}
    panel.field=8;panel.key('\r',{},T/1000);for(int columns:{60,80,120})for(int rows:{12,24})frame(panel,columns,rows);
    panel.key(27,{},T/1000);panel.key('v',{},T/1000);panel.reviewOffset=100000;for(int columns:{60,80,120})for(int rows:{12,24})frame(panel,columns,rows);
    SettingsPanel settings;settings.open({});settings.group=2;settings.groupsFocused=false;settings.field=1;CHECK(settings.fields()==5&&settings.key('\r',{})==SettingsResult::OpenTariff);
    openTariffControls(&settings,&store);CHECK(settings.tariff.visible);CHECK(settings.key(3,{})==SettingsResult::Quit);
    std::filesystem::remove_all(root);
    if(argc==2){
        CHECK(configureCivilTimeData(std::filesystem::u8path(argv[1]),tariffTzdbVersion)==CivilStatus::Ok);
        panel.open(original);
        enter(panel,"plan");
        panel.field=5;
        CHECK(panel.render(120,24,ColorLevel::None).find("[Z] Named/UTC clock")!=std::string::npos);
        panel.key('z',{},T/1000);
        CHECK(panel.draft.find("schema_version")->text=="4");
        CHECK(panel.draft.find("opening")->find("time_database_version")->text==tariffTzdbVersion);
        enter(panel,"clock");enter(panel,"zone");type(panel,"Etc/UTC");
        TariffDeclaration validated;
        CHECK(decodeTariffDeclaration(panel.draft,&validated)&&validated.plan.clock.zone=="Etc/UTC");
        CHECK(validated.opening.timeDatabaseVersion==tariffTzdbVersion);
        panel.key(27,{},T/1000);panel.key(27,{},T/1000);
        panel.field=5;
        panel.key('m',{},T/1000);
        const auto *reset=panel.draft.find("reset_policy");
        CHECK(reset->find("clock")->find("zone")->text=="Etc/UTC"&&panel.draft.find("schema_version")->text=="4");
        enter(panel,"reset_policy");
        panel.field=1;
        panel.key('z',{},T/1000);
        CHECK(panel.draft.find("reset_policy")->find("clock")->find("kind")->text=="utc");
        CHECK(panel.draft.find("schema_version")->text=="4");
        panel.key(27,{},T/1000);panel.key('m',{},T/1000);
        enter(panel,"plan");panel.field=5;panel.key('z',{},T/1000);
        CHECK(!panel.dirty()&&panel.draft.find("opening")->find("time_database_version")->text=="explicit-fixed-clock");

        // New York has shifted to daylight time by this cycle's end, while its March opening still matches UTC-05.
        auto accrued=original;
        constexpr int64_t marchStart=1709269200000, aprilStart=1711944000000,
            oldEnd=aprilStart+3600000, through=aprilStart-3600000;
        set(accrued,"schema_version","3");
        set(accrued,"reset_policy",R"({"monthly_day":1,"clock":{"kind":"fixed_standard","fixed_offset_seconds":-18000},"daily_fee_allocation":"prorate_by_cycle_minutes"})");
        auto &opening=member(accrued,"opening");
        set(opening,"cycle_begin_utc_ms",std::to_string(marchStart));
        set(opening,"cycle_end_utc_ms",std::to_string(oldEnd));
        set(opening,"through_utc_ms",std::to_string(through));
        set(opening,"imported_kwh","\"2\"");
        set(opening,"accrued_subtotal","\"744\"");
        auto &price=member(accrued,"plan");
        set(price,"tiers","\"none\"");
        set(price,"cycle_charge","\"0\"");
        set(price,"subtotal_multiplier","\"1\"");
        auto &rate=member(price,"revisions").items.front();
        set(rate,"begin_utc_ms",std::to_string(marchStart-5*3600000));
        set(rate,"end_utc_ms",std::to_string(oldEnd));
        set(rate,"published_utc_ms",std::to_string(marchStart));
        set(rate,"time_mode","\"constant\"");
        set(rate,"calendar","[]");
        set(rate,"bands","[]");
        set(rate,"base_rate_per_kwh","\"0\"");
        set(rate,"charge_per_civil_day","\"24\"");
        const auto prefixTotal=[&]() {
            auto cycleOpening=validated.opening;
            cycleOpening.throughUtcMs=marchStart;
            cycleOpening.importedKwh={};
            cycleOpening.accruedSubtotal={};
            const auto priced=accrueBill(validated.plan,cycleOpening,
                {cycleOpening.evidence,{{marchStart,through,{}}}},through,T,{},validated.dailyFeeAllocation());
            std::string amount;
            CHECK(priced.status==ElectricityStatus::Ok&&priced.provisionalTotal&&priced.provisionalTotal->format(6,&amount));
            return amount;
        };
        CHECK(decodeTariffDeclaration(accrued,&validated));
        CHECK(prefixTotal()=="744.000000");
        panel.open(accrued);
        enter(panel,"reset_policy");panel.field=1;panel.key('z',{},T/1000);
        enter(panel,"clock");enter(panel,"zone");type(panel,"America/New_York");
        panel.key('g',{},T/1000);
        const auto *changed=panel.draft.find("opening");
        CHECK(changed->find("cycle_begin_utc_ms")->text==std::to_string(marchStart));
        CHECK(changed->find("cycle_end_utc_ms")->text==std::to_string(aprilStart));
        CHECK(changed->find("imported_kwh")->text=="2"&&changed->find("accrued_subtotal")->text.empty());
        CHECK(panel.message.find("Re-enter the subtotal")!=std::string::npos);
        CHECK(!decodeTariffDeclaration(panel.draft,&validated));
        panel.key('\r',{},T/1000);
        type(panel,"743");
        CHECK(decodeTariffDeclaration(panel.draft,&validated));
        CHECK(prefixTotal()=="743.000000");
        auto settled=accrueBill(validated.plan,validated.opening,
            {validated.opening.evidence,{{through,aprilStart,{}}}},aprilStart,T,{},validated.dailyFeeAllocation());
        std::string total;
        CHECK(settled.status==ElectricityStatus::Ok&&settled.settledTotal&&settled.settledTotal->format(6,&total)&&total=="743.000000");
    }
    {
        auto selected=fixture();
        auto &plan=member(selected,"plan");
        set(plan,"tiers","\"none\"");
        member(member(plan,"revisions").items[0],"bands").items.clear();
        auto &opening=member(selected,"opening");
        set(opening,"through_utc_ms",std::to_string(T+1800000));
        set(opening,"imported_kwh","\"0.025\"");
        set(opening,"accrued_subtotal","\"0.006\"");
        TariffSettingsPanel roundingPanel;
        roundingPanel.open(selected);
        enter(roundingPanel,"plan");
        enter(roundingPanel,"rounding");
        CHECK(roundingPanel.path()=="opening"&&roundingPanel.message.find("Re-enter")!=std::string::npos);
        CHECK(roundingPanel.draft.find("plan")->find("rounding")->text==meterHalfHourRounding);
        CHECK(roundingPanel.draft.find("opening")->find("imported_kwh")->text=="0.025");
        TariffDeclaration revised;
        CHECK(!decodeTariffDeclaration(roundingPanel.draft,&revised));
        roundingPanel.key('\r',{},T/1000);
        type(roundingPanel,"0.004");
        CHECK(decodeTariffDeclaration(roundingPanel.draft,&revised));
        CHECK(revised.plan.rounding==TariffRounding::MeterHalfHourKwhHalfEven2);
        CHECK(compareTariffCycles(revised,{dec("0"),dec("1")},1,T+1800000).status==
              ElectricityStatus::Unsupported);
    }
    return finishTests("tariff declaration");
}
