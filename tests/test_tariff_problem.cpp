// Diagnostic metadata only. Existing paths/messages remain legacy originals; classifications admit no source/model fact.
#include "product/tariff_declaration.h"
#include "product_test_util.h"
#include <fstream>
using namespace om;
namespace {
using Kind=TariffProblemKind;
JsonValue json(const std::string &text){JsonValue value;CHECK(parseJsonStrict(text,&value,nullptr));return value;}
JsonValue &member(JsonValue &value,const std::string &name){for(auto &entry:value.members)if(entry.first==name)return entry.second;throw 1;}
void set(JsonValue &value,const std::string &name,const std::string &encoded){member(value,name)=json(encoded);}
JsonValue fixture(){std::ifstream stream("config/tariff-declaration.example.json");const std::string text{std::istreambuf_iterator<char>(stream),{}};return json(text);}
TariffProblem expect(const JsonValue &value,Kind kind,const std::string &path){TariffDeclaration output;output.country="unchanged-sentinel";TariffProblem problem;
    CHECK(!decodeTariffDeclaration(value,&output,&problem)&&output.country=="unchanged-sentinel"&&problem.kind==kind&&problem.path==path&&!problem.message.empty());
    if(kind!=Kind::Choice)CHECK(problem.choices.empty());
    if(kind!=Kind::ModelStatus)CHECK(!problem.modelStatus);
    return problem;
}
void classifications(){
    const auto original=fixture();auto value=original;value.members.pop_back();expect(value,Kind::MissingFields,"");
    value=original;value.members.back().first="synthetic_unknown";expect(value,Kind::SchemaFields,"");
    value=original;set(value,"country","\"\"");expect(value,Kind::Label,"country");
    value=original;set(value,"confirmed_utc","-1");expect(value,Kind::Integer,"confirmed_utc");
    value=original;set(member(value,"plan"),"cycle_charge","\"1.00\"");expect(value,Kind::Decimal,"plan.cycle_charge");
    value=original;set(member(value,"plan"),"tiers","\"unknown\"");const auto choice=expect(value,Kind::Choice,"plan.tiers");CHECK(choice.choices==std::vector<std::string>({"none","progressive","all_units"})&&choice.message=="Choose none / progressive / all_units.");
    value=original;set(member(member(value,"plan"),"clock"),"kind","\"unknown\"");CHECK(expect(value,Kind::Choice,"plan.clock.kind").choices==std::vector<std::string>({"utc","fixed_standard"}));
    value=original;member(member(value,"plan"),"revisions").items.resize(17);expect(value,Kind::Collection,"plan.revisions");
    TariffProblem noOutput;CHECK(!decodeTariffDeclaration(original,nullptr,&noOutput)&&noOutput.kind==Kind::NoOutput&&noOutput.path.empty()&&noOutput.choices.empty()&&!noOutput.modelStatus);
    value=original;member(value,"country").text=std::string(tariffDeclarationMaxBytes,'x');expect(value,Kind::Oversized,"");
    value=original;set(value,"schema_version","0");expect(value,Kind::SchemaVersion,"schema_version");
    value=original;set(value,"acknowledged","false");expect(value,Kind::Acknowledgement,"acknowledged");
    value=original;set(value,"confirmed_utc","0");expect(value,Kind::Confirmation,"confirmed_utc");
    value=original;set(member(value,"plan"),"currency","\"US\"");expect(value,Kind::Currency,"plan.currency");
    value=original;set(member(member(value,"plan"),"clock"),"fixed_offset_seconds","60");expect(value,Kind::FixedOffset,"plan.clock.fixed_offset_seconds");
    value=original;set(member(member(member(value,"plan"),"revisions").items[0],"calendar").items[0],"date","\"not-a-date\"");expect(value,Kind::Date,"plan.revisions[0].calendar[0].date");
    value=original;set(member(value,"opening"),"imported_kwh","\"-1\"");expect(value,Kind::OpeningQuantity,"opening.imported_kwh");
    value=original;set(member(value,"opening"),"cycle_end_utc_ms","1800000000000");const auto model=expect(value,Kind::ModelStatus,"plan/opening");CHECK(model.modelStatus&&*model.modelStatus!=ElectricityStatus::Ok&&model.message==electricityStatusName(*model.modelStatus));
    value=original;set(member(member(value,"plan"),"revisions").items[0],"published_utc_ms","1800000000001");expect(value,Kind::FuturePublication,"plan.revisions");
    for(const auto &text:std::vector<std::string>{"{","{\"x\":1,\"x\":2}",std::string(tariffDeclarationMaxBytes+1,' ')}){JsonValue output;output.type=JsonValue::Type::String;output.text="unchanged-sentinel";TariffProblem problem;
        CHECK(!parseTariffDeclaration(text,&output,&problem)&&output.text=="unchanged-sentinel"&&problem.kind==Kind::InvalidJson&&problem.path.empty()&&problem.choices.empty()&&!problem.modelStatus);}
    TariffProblem invalidOutput;CHECK(!parseTariffDeclaration("{}",nullptr,&invalidOutput)&&invalidOutput.kind==Kind::InvalidJson);
    // DuplicateField and ArithmeticCapacity retain legacy defensive sites; valid current schema/decimal bounds do not reach them.
    TariffProblem legacy{"synthetic-path","original refusal"};CHECK(legacy.kind==Kind::Unknown&&legacy.choices.empty()&&!legacy.modelStatus);
    TariffProblem cleared=choice;cleared.modelStatus=model.modelStatus;TariffDeclaration decoded;
    CHECK(decodeTariffDeclaration(original,&decoded,&cleared)&&cleared.path.empty()&&cleared.message.empty()&&cleared.kind==Kind::Unknown&&cleared.choices.empty()&&!cleared.modelStatus);
    CHECK(decoded.country=="synthetic-country"&&decoded.opening.planKey==decoded.plan.key);
    CHECK(!decodeTariffDeclaration(value,&decoded,nullptr));
}
} // namespace
int main(){classifications();return finishTests("display-neutral tariff problem classifications with preserved legacy refusal/source boundaries");}
