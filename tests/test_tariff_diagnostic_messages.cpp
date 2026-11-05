// Explicit refusal descriptions only: classification/display never supplies choice, supplier, model or source admission.
#include "product/terminal_settings_message.h"
#include "product/tariff_settings.h"
#include "product_test_util.h"
#include <fstream>
#include <set>
using namespace om;
namespace {
using Key=i18n::TariffDiagnosticMessageKey;
const std::array<std::string,20> english={{"Missing/unknown fields; unsupported rules are never discarded.","Missing/unknown fields; reopen the documented schema.","Duplicate field.","Use 1..128 printable ASCII characters, no surrounding spaces or credentials.","Expected a bounded nonnegative integer in the stated units.","Enter a canonical decimal string, <=38 digits / 18 decimals. Empty is missing; explicit zero is valid.","Choose {choices}.","Collection bound exceeded (16 revisions, 32 calendar/bands, 256 dynamic, 512 total rows).","No declaration output.","Declaration exceeds 64 KiB.","Unsupported schema.","Acknowledge declared-model exclusions before saving.","Explicit review confirmation required.","Three uppercase letters required; no currency/FX inference.","Whole-minute STANDARD offset within +/-18h; UTC requires zero.","Use YYYY-MM-DD or clear for no exact-date condition.","Nonnegative whole-meter quantity required.","Arithmetic capacity exceeded.","Publication cannot be after local confirmation.","Invalid/oversized JSON; file preserved."}};
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::tariffDiagnosticMessageKeyCount==20&&i18n::tariffDiagnosticLocalePacks().size()==16);std::set<std::string> identities;
    for(std::size_t key=0;key<20;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::tariffDiagnosticMessageKeyName(k))).second&&i18n::tariffDiagnosticMessageKeyName(k).find("tariff.diagnostic.")==0&&i18n::tariffDiagnosticMessage("en",k).text==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::tariffDiagnosticLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<20;++key){const auto message=i18n::tariffDiagnosticMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text)&&i18n::templateParameters(message.text,&parameters));
            CHECK(parameters==(key==6?std::vector<std::string_view>{"choices"}:std::vector<std::string_view>{}));
            for(const auto *token:{"ASCII","1..128","<=38","18","16","32","256","512","64 KiB","FX","STANDARD","+/-18h","UTC","YYYY-MM-DD","JSON"})if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::tariffDiagnosticEnglish(Key::Choice,{{"choices","{choices}"}})=="Choose {choices}.");
    CHECK(i18n::tariffDiagnosticEnglish(Key::Choice).empty()&&i18n::tariffDiagnosticEnglish(Key::Choice,{{"extra","x"}}).empty()&&i18n::tariffDiagnosticEnglish(Key::Label,{{"extra","x"}}).empty());
    CHECK(i18n::tariffDiagnosticMessageKeyName(Key::Count).empty()&&i18n::tariffDiagnosticMessage("en",Key::Count).text.empty()&&i18n::tariffDiagnosticEnglish(Key::Count).empty());
}
void native(){
    using Kind=TariffProblemKind;const std::array<Kind,20> kinds={{Kind::MissingFields,Kind::SchemaFields,Kind::DuplicateField,Kind::Label,Kind::Integer,Kind::Decimal,Kind::Choice,Kind::Collection,Kind::NoOutput,Kind::Oversized,Kind::SchemaVersion,Kind::Acknowledgement,Kind::Confirmation,Kind::Currency,Kind::FixedOffset,Kind::Date,Kind::OpeningQuantity,Kind::ArithmeticCapacity,Kind::FuturePublication,Kind::InvalidJson}};
    for(std::size_t key=0;key<20;++key){const auto mapping=tariffDiagnosticKey(kinds[key]);CHECK(mapping&&*mapping==static_cast<Key>(key));TariffProblem problem;
        problem.path="synthetic-path";problem.kind=kinds[key];problem.message=key==6?"Choose none / progressive / all_units.":english[key];if(key==6)problem.choices={"none","progressive","all_units"};
        const auto before=problem.message;CHECK(tariffDiagnosticReasonEnglish(problem)==before&&problem.path=="synthetic-path"&&problem.message==before&&problem.kind==kinds[key]);
        problem.message="changed original {choices}";CHECK(tariffDiagnosticReasonEnglish(problem)==problem.message);
        if(key==6){problem.choices.clear();CHECK(tariffDiagnosticReasonEnglish(problem)==problem.message);problem.choices={""};CHECK(tariffDiagnosticReasonEnglish(problem)==problem.message);}}
    for(const auto kind:{Kind::Unknown,Kind::ModelStatus,static_cast<Kind>(99)})for(const auto &raw:std::vector<std::string>{"",std::string(5000,'x'),std::string("a\x1b\0b",4),"{choices}"}){TariffProblem problem;problem.kind=kind;problem.message=raw;CHECK(!tariffDiagnosticKey(kind)&&tariffDiagnosticReasonEnglish(problem)==raw);}
    std::ifstream stream("config/tariff-declaration.example.json");const std::string text{std::istreambuf_iterator<char>(stream),{}};JsonValue original;TariffProblem problem;CHECK(parseTariffDeclaration(text,&original,&problem));
    TariffSettingsPanel panel;panel.open(original);for(auto &entry:panel.draft.members)if(entry.first=="country")entry.second.text.clear();const auto before=tariffJson(panel.active);
    panel.key('v',{},1800000000);panel.key('\r',{},1800000000);CHECK(panel.message=="country: "+english[3]&&panel.page==TariffSettingsPage::Browse&&tariffJson(panel.active)==before&&panel.dirty());
    panel.open(original);panel.key('v',{},1800000000);auto reject=[](const JsonValue &,std::string *error){*error="synthetic raw store callback";return false;};panel.key('\r',reject,1800000000);
    CHECK(panel.message=="synthetic raw store callback"&&tariffJson(panel.active)==before&&panel.page==TariffSettingsPage::Review&&panel.key(3,{},1800000000)==TariffSettingsResult::Quit);
}
TerminalTariffDiagnosticMessageRequest requestFor(const i18n::TariffDiagnosticLocalePack &pack,Key key){TerminalTariffDiagnosticMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;
    if(key==Key::Choice){request.arguments={{"choices",{{"none / progressive / all_units",{},true},Tone::Canary}}};}return request;}
void matrix(){
    for(const auto &pack:i18n::tariffDiagnosticLocalePacks())for(std::size_t key=0;key<20;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalTariffDiagnosticMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:Tone::Canary));}}
            if(!request.arguments.empty()){std::string actual;for(const auto &run:line.line.runs)if(!run.content.control&&!line.metadata.sources[run.content.sourceSpan].empty())actual+=run.content.text;CHECK(actual.empty()||actual==request.arguments[0].value.content.text);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalTariffDiagnosticMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){auto expected=std::string(i18n::tariffDiagnosticMessage(recover?"en":pack.id,request.key).text);const auto at=expected.find("{choices}");if(at!=expected.npos)expected.replace(at,9,request.arguments[0].value.content.text);
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=39;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalTariffDiagnosticMessage(request,policy,128,&rows)&&renderWrappedTerminalTariffDiagnosticMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=39);}
    }
}
void refused(const TerminalTariffDiagnosticMessageRequest &bad,const TerminalTextPolicy &policy){
    TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
    PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
    CHECK(!layoutTerminalTariffDiagnosticMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
    CHECK(!wrapTerminalTariffDiagnosticMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
    CHECK(!renderTerminalTariffDiagnosticMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
    CHECK(!renderWrappedTerminalTariffDiagnosticMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});
}
void failures(){
    for(const auto &pack:i18n::tariffDiagnosticLocalePacks())for(std::size_t key=0;key<20;++key)for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){const auto original=requestFor(pack,static_cast<Key>(key));TerminalTextPolicy policy;policy.order=order;auto bad=original;
        bad.arguments.push_back({"extra",{{"x",{},true},Tone::Canary}});refused(bad,policy);
        if(!original.arguments.empty()){bad=original;bad.arguments.clear();refused(bad,policy);bad=original;bad.arguments.push_back(bad.arguments[0]);refused(bad,policy);
            for(unsigned poison=0;poison<8;++poison){bad=original;auto &span=bad.arguments[0].value.content;
                if(poison==0)span.invariant=false;
                if(poison==1)span.text.clear();
                if(poison==2)span.text="\x1b";
                if(poison==3)span.text=std::string(257,'x');
                if(poison==4)span.ascii=std::string(257,'x');
                if(poison==5){span.text="\xC3";span.ascii="safe";}
                if(poison==6)span.ascii="\x1b";
                if(poison==7)span.ascii="substituted";
                refused(bad,policy);}}
    }
    TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=1024;TerminalMessageLine line;auto request=requestFor(i18n::tariffDiagnosticLocalePacks()[0],Key::Choice);
    for(const auto &opaque:std::vector<std::string>{"{choices}","not-an-admitted-choice",std::string(256,'x')}){request.arguments[0].value.content.text=opaque;CHECK(layoutTerminalTariffDiagnosticMessage(request,policy,&line)&&unisolate(line.line.line.text)=="Choose "+opaque+".");}
    policy.columns=19;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalTariffDiagnosticMessage(request,policy,128,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(layoutTerminalTariffDiagnosticMessage(request,policy,&line));for(const auto &run:line.line.runs)if(!run.content.control)CHECK(line.metadata.sources[run.content.sourceSpan].empty());
    policy.columns=1024;request={};request.language="en-US";CHECK(layoutTerminalTariffDiagnosticMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    request.key=Key::Count;refused(request,policy);request.key=Key::Label;request.language="en@private";refused(request,policy);request.language="en";request.literalTone=static_cast<Tone>(99);refused(request,policy);
    CHECK(!layoutTerminalTariffDiagnosticMessage({},policy,nullptr)&&!wrapTerminalTariffDiagnosticMessage({},policy,1,nullptr)&&!renderTerminalTariffDiagnosticMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalTariffDiagnosticMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack explicitly classified tariff refusal descriptions without choice, model, supplier or source admission");}
