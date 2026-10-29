#include "product/tariff_settings.h"
#include "product/settings.h"
#include "product_test_util.h"
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
}
int main(){
    auto original=fixture();TariffDeclaration d;TariffProblem error;CHECK(decodeTariffDeclaration(original,&d,&error));
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
    // Focus remains visible at the supported compact terminal sizes, including deep table routes.
    panel.open(original);enter(panel,"plan");enter(panel,"revisions");panel.key('\r',{},T/1000);enter(panel,"calendar");panel.key('\r',{},T/1000);
    for(int columns:{60,80,120})for(int rows:{12,24})for(size_t i=0;i<9;++i){panel.field=i;frame(panel,columns,rows);CHECK(panel.render(columns,rows,ColorLevel::None).find("> ")!=std::string::npos);}
    panel.field=8;panel.key('\r',{},T/1000);for(int columns:{60,80,120})for(int rows:{12,24})frame(panel,columns,rows);
    panel.key(27,{},T/1000);panel.key('v',{},T/1000);panel.reviewOffset=100000;for(int columns:{60,80,120})for(int rows:{12,24})frame(panel,columns,rows);
    SettingsPanel settings;settings.open({});settings.group=2;settings.groupsFocused=false;settings.field=1;CHECK(settings.fields()==4&&settings.key('\r',{})==SettingsResult::OpenTariff);
    openTariffControls(&settings,&store);CHECK(settings.tariff.visible);CHECK(settings.key(3,{})==SettingsResult::Quit);
    std::filesystem::remove_all(root);
    return finishTests("tariff declaration");
}
