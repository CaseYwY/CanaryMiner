// Human arithmetic/coverage descriptions only. Source equality and synthetic enums prove no observation or permission.
#include "product/terminal_settings_message.h"
#include "product/tariff_settings.h"
#include "product_test_util.h"
#include <fstream>
#include <set>
using namespace om;
namespace {
using Key=i18n::ElectricityStatusMessageKey;
const std::array<std::string,11> english={{"declared arithmetic available (not supplier-certified)","partial sample-and-hold coverage","missing configuration/state/coverage/time data","invalid identity/unit/interval/state","unsupported rule/rounding/settlement horizon","dynamic price gap; no carry-forward","overlapping/unresolved rate definitions","rate published after decision instant","regressed or inconsistent sample clocks","bounded arithmetic/input/evaluation capacity exceeded","unknown electricity status"}};
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::electricityStatusMessageKeyCount==11&&i18n::electricityStatusLocalePacks().size()==16);std::set<std::string> identities;
    for(std::size_t key=0;key<11;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::electricityStatusMessageKeyName(k))).second&&i18n::electricityStatusMessageKeyName(k).find("electricity.status.")==0&&i18n::electricityStatusEnglish(k)==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::electricityStatusLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<11;++key){const auto message=i18n::electricityStatusMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text)&&i18n::templateParameters(message.text,&parameters)&&parameters.empty());}}
    CHECK(i18n::electricityStatusMessageKeyName(Key::Count).empty()&&i18n::electricityStatusMessage("en",Key::Count).text.empty()&&i18n::electricityStatusEnglish(Key::Count).empty());
}
JsonValue &member(JsonValue &value,const std::string &name){for(auto &entry:value.members)if(entry.first==name)return entry.second;throw 1;}
void native(){
    const std::array<ElectricityStatus,10> statuses={{ElectricityStatus::Ok,ElectricityStatus::Partial,ElectricityStatus::Unavailable,ElectricityStatus::Invalid,ElectricityStatus::Unsupported,ElectricityStatus::MissingPrice,ElectricityStatus::Conflict,ElectricityStatus::NotYetKnown,ElectricityStatus::ClockFault,ElectricityStatus::Capacity}};
    for(std::size_t key=0;key<10;++key){const auto status=statuses[key];const auto mapping=electricityStatusSourceKey(status);CHECK(mapping&&*mapping==static_cast<Key>(key)&&english[key]==electricityStatusName(status));
        TariffProblem problem;problem.kind=TariffProblemKind::ModelStatus;problem.modelStatus=status;problem.message=english[key];problem.path="synthetic-path";
        CHECK(!tariffDiagnosticKey(problem.kind)&&tariffDiagnosticReasonEnglish(problem)==problem.message&&problem.modelStatus==status&&problem.path=="synthetic-path");
        for(const auto &raw:std::vector<std::string>{"",std::string(5000,'x'),std::string("a\x1b\0b",4),"changed original {state}"}){problem.message=raw;CHECK(electricityStatusReasonEnglish(status,raw)==raw&&tariffDiagnosticReasonEnglish(problem)==raw);}
        problem.kind=TariffProblemKind::Unknown;CHECK(tariffDiagnosticReasonEnglish(problem)==problem.message);
    }
    const auto unknown=static_cast<ElectricityStatus>(99);CHECK(!electricityStatusSourceKey(unknown)&&english[10]==electricityStatusName(unknown)&&electricityStatusReasonEnglish(unknown,"raw original")=="raw original");
    TariffProblem missing;missing.kind=TariffProblemKind::ModelStatus;missing.message="missing status payload";CHECK(tariffDiagnosticReasonEnglish(missing)==missing.message);
    std::ifstream stream("config/tariff-declaration.example.json");const std::string text{std::istreambuf_iterator<char>(stream),{}};JsonValue original;TariffProblem problem;CHECK(parseTariffDeclaration(text,&original,&problem));
    auto invalid=original;member(member(invalid,"opening"),"cycle_end_utc_ms").text="1800000000000";TariffDeclaration output;output.country="unchanged-sentinel";
    CHECK(!decodeTariffDeclaration(invalid,&output,&problem)&&problem.kind==TariffProblemKind::ModelStatus&&problem.modelStatus&&*problem.modelStatus!=ElectricityStatus::Ok&&output.country=="unchanged-sentinel");
    CHECK(tariffDiagnosticReasonEnglish(problem)==problem.message&&problem.message==electricityStatusName(*problem.modelStatus));
    TariffSettingsPanel panel;panel.open(original);panel.draft=invalid;const auto before=tariffJson(panel.active);panel.key('v',{},1800000000);panel.key('\r',{},1800000000);
    CHECK(panel.message==problem.path+": "+problem.message&&tariffJson(panel.active)==before&&panel.dirty()&&panel.page==TariffSettingsPage::Browse&&panel.key(3,{},1800000000)==TariffSettingsResult::Quit);
}
void matrix(){
    for(const auto &pack:i18n::electricityStatusLocalePacks())for(std::size_t key=0;key<11;++key){TerminalElectricityStatusMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<Key>(key);request.literalTone=Tone::Green;
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalElectricityStatusMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size()&&line.metadata.sources[run.content.sourceSpan].empty()&&run.tone==Tone::Green);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalElectricityStatusMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover))CHECK(unisolate(joined)==i18n::electricityStatusMessage(recover?"en":pack.id,request.key).text&&!line.line.line.clipped);
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalElectricityStatusMessage(request,policy,128,&rows)&&renderWrappedTerminalElectricityStatusMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<10;++mode){TerminalElectricityStatusMessageRequest bad;TerminalTextPolicy policy;policy.order=order;
        if(mode==0)bad.key=Key::Count;
        if(mode==1)bad.language="en@private";
        if(mode==2)bad.language="";
        if(mode==3)bad.language=std::string(36,'x');
        if(mode==4)bad.literalTone=static_cast<Tone>(99);
        if(mode==5)policy.columns=4097;
        if(mode==6)policy.direction=static_cast<TerminalTextDirection>(99);
        if(mode==7)policy.ambiguousCells=0;
        if(mode==8)policy.order=static_cast<TerminalTextOrder>(99);
        if(mode==9)policy.ambiguousCells=3;
        TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
        PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
        CHECK(!layoutTerminalElectricityStatusMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalElectricityStatusMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalElectricityStatusMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalElectricityStatusMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    TerminalElectricityStatusMessageRequest request;request.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalElectricityStatusMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    policy.columns=3;request.key=Key::Capacity;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalElectricityStatusMessage(request,policy,1,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalElectricityStatusMessage({},policy,nullptr)&&!wrapTerminalElectricityStatusMessage({},policy,1,nullptr)&&!renderTerminalElectricityStatusMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalElectricityStatusMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack human electricity arithmetic and coverage status descriptions without source or supplier qualification");}
