// Whole diagnostic envelopes, not error parsing, safe disclosure, validation or action authority.
#include "product/terminal_settings_message.h"
#include "product/tariff_settings.h"
#include "product_test_util.h"
#include <fstream>
#include <set>
using namespace om;
namespace {
using Key=i18n::TariffErrorMessageKey;
const std::array<std::string,5> english={{"{path}: {reason}",": {reason}","{path}: {reason}",": {reason}","{reason}"}};
bool human(Key key){return key==Key::HumanPath||key==Key::HumanRoot;}
std::size_t maximum(Key key,const std::string &name){return name=="path"?512:key==Key::Raw?4096:1024;}
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::tariffErrorMessageKeyCount==5&&i18n::tariffErrorLocalePacks().size()==16);std::set<std::string> identities;
    for(std::size_t key=0;key<5;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::tariffErrorMessageKeyName(k))).second&&i18n::tariffErrorMessageKeyName(k).find("tariff.error.")==0&&i18n::tariffErrorMessage("en",k).text==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::tariffErrorLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<5;++key){const auto message=i18n::tariffErrorMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> names;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text)&&i18n::templateParameters(message.text,&names));
            CHECK(names==((key==0||key==2)?std::vector<std::string_view>{"path","reason"}:std::vector<std::string_view>{"reason"}));}}
    for(const auto &raw:std::vector<std::string>{"",std::string(6000,'x'),std::string("a\x1b\0b",4),u8"ع","{reason}: {path}"}){
        CHECK(i18n::tariffErrorEnglish(Key::Raw,{{"reason",raw}})==raw);
        CHECK(i18n::tariffErrorEnglish(Key::OriginalRoot,{{"reason",raw}})==": "+raw);
        CHECK(i18n::tariffErrorEnglish(Key::OriginalPath,{{"reason",raw},{"path",raw}})==raw+": "+raw);}
    CHECK(i18n::tariffErrorEnglish(Key::HumanPath,{{"reason","{path}"},{"path","{reason}"}})=="{reason}: {path}");
    CHECK(i18n::tariffErrorEnglish(Key::Raw).empty()&&i18n::tariffErrorEnglish(Key::Raw,{{"reason","a"},{"reason","b"}}).empty()&&i18n::tariffErrorEnglish(Key::OriginalRoot,{{"reason","a"},{"path","b"}}).empty());
    CHECK(i18n::tariffErrorMessageKeyName(Key::Count).empty()&&i18n::tariffErrorMessage("en",Key::Count).text.empty()&&i18n::tariffErrorEnglish(Key::Count).empty());
}
void native(){
    std::ifstream stream("config/tariff-declaration.example.json");const std::string text{std::istreambuf_iterator<char>(stream),{}};JsonValue original;TariffProblem problem;CHECK(parseTariffDeclaration(text,&original,&problem));
    TariffSettingsPanel panel;panel.open(original);for(auto &entry:panel.draft.members)if(entry.first=="country")entry.second.text.clear();const auto before=tariffJson(panel.active),draft=tariffJson(panel.draft);unsigned saves=0;
    auto save=[&](const JsonValue &,std::string *){++saves;return false;};panel.key('v',save,1800000000);const auto review=panel.review;panel.key('\r',save,1800000000);
    CHECK(panel.message=="country: "+i18n::tariffDiagnosticEnglish(i18n::TariffDiagnosticMessageKey::Label)&&panel.page==TariffSettingsPage::Browse&&tariffJson(panel.active)==before&&tariffJson(panel.draft)==draft&&panel.review==review&&!saves);
    panel.open({});panel.key('v',save,1800000000);panel.key('\r',save,1800000000);CHECK(panel.message==": Missing/unknown fields; unsupported rules are never discarded."&&!saves&&panel.page==TariffSettingsPage::Browse);
    for(const auto &raw:std::vector<std::string>{"synthetic: {reason} {path}",std::string(6000,'x'),std::string("a\x1b\0b",4),u8"ع",""}){
        panel.open(original);panel.key('v',{},1800000000);const auto active=tariffJson(panel.active),unchanged=tariffJson(panel.draft);const auto complete=panel.review;
        auto reject=[&](const JsonValue &,std::string *error){++saves;*error=raw;return false;};panel.key('\r',reject,1800000000);
        CHECK(panel.message==(raw.empty()?i18n::tariffNoticeEnglish(i18n::TariffNoticeMessageKey::SaveUnavailable):raw)&&panel.page==TariffSettingsPage::Review&&tariffJson(panel.active)==active&&tariffJson(panel.draft)==unchanged&&panel.review==complete);
        panel.key(27,{},1800000000);CHECK(panel.page==TariffSettingsPage::Browse&&panel.key(3,{},1800000000)==TariffSettingsResult::Quit);}
    CHECK(saves==5);
    panel.open(original);panel.key('v',{},1800000000);panel.message="retained raw {reason}";panel.key('\r',{},1800000000);CHECK(panel.message=="retained raw {reason}"&&panel.page==TariffSettingsPage::Review);
    panel.key(27,{},1800000000);CHECK(panel.key(27,{},1800000000)==TariffSettingsResult::Close&&!panel.visible);
}
TerminalTariffErrorMessageRequest requestFor(const i18n::TariffErrorLocalePack &pack,Key key){
    TerminalTariffErrorMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;
    request.arguments={{"reason",{{human(key)?"human description {path}":"opaque: {path}",{},!human(key)},Tone::Red}}};
    if(key==Key::OriginalPath||key==Key::HumanPath)request.arguments.push_back({"path",{{"revisions[0].key",{},true},Tone::Canary}});
    return request;
}
void matrix(){
    for(const auto &pack:i18n::tariffErrorLocalePacks())for(std::size_t key=0;key<5;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalTariffErrorMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:name=="path"?Tone::Canary:Tone::Red));}}
            for(const auto &arg:request.arguments)if(arg.value.content.invariant){std::string actual;for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]==arg.name)actual+=run.content.text;CHECK(actual.empty()||actual==arg.value.content.text);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            for(auto color:{ColorLevel::None,ColorLevel::Basic16,ColorLevel::Palette256,ColorLevel::TrueColor}){PaintedTerminalMessageLine painted;CHECK(renderTerminalTariffErrorMessage(request,policy,color,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);}
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){const auto text=i18n::tariffErrorMessage(recover?"en":pack.id,request.key).text;std::string expected;std::size_t at=0;
                while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){expected+=text.substr(at);break;}expected+=text.substr(at,start-at);const auto end=text.find('}',start);const auto name=text.substr(start+1,end-start-1);
                    const auto arg=std::find_if(request.arguments.begin(),request.arguments.end(),[&](const auto &a){return a.name==name;});CHECK(arg!=request.arguments.end());expected+=arg->value.content.text;at=end+1;}
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=29;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalTariffErrorMessage(request,policy,128,&rows)&&renderWrappedTerminalTariffErrorMessage(request,policy,128,ColorLevel::TrueColor,&painted)&&rows.rows.size()==painted.rows.size()&&rows.metadata.sources==painted.metadata.sources);
            for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==unpaint(painted.rows[i])&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=29);}
    }
}
void refused(const TerminalTariffErrorMessageRequest &bad,const TerminalTextPolicy &policy){
    TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
    PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
    CHECK(!layoutTerminalTariffErrorMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
    CHECK(!wrapTerminalTariffErrorMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
    CHECK(!renderTerminalTariffErrorMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
    CHECK(!renderWrappedTerminalTariffErrorMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});
}
void failures(){
    for(const auto &pack:i18n::tariffErrorLocalePacks())for(std::size_t key=0;key<5;++key)for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
        const auto original=requestFor(pack,static_cast<Key>(key));TerminalTextPolicy policy;policy.order=order;auto bad=original;
        bad.arguments.push_back({"extra",{{"x",{},true},Tone::Red}});refused(bad,policy);bad=original;bad.arguments.pop_back();refused(bad,policy);bad=original;bad.arguments.push_back(bad.arguments[0]);refused(bad,policy);
        for(std::size_t i=0;i<original.arguments.size();++i)for(unsigned poison=0;poison<10;++poison){bad=original;auto &span=bad.arguments[i].value.content;const auto max=maximum(bad.key,bad.arguments[i].name);
            if(poison==0)span.invariant=!span.invariant;
            if(poison==1)span.text.clear();
            if(poison==2)span.text="\x1b";
            if(poison==3)span.text=std::string(max+1,'x');
            if(poison==4)span.ascii=std::string(max+1,'x');
            if(poison==5){span.text="\xC3";span.ascii="safe";}
            if(poison==6)span.ascii="\x1b";
            if(poison==7){if(span.invariant)span.ascii="substituted";else span.atom=static_cast<TerminalTextAtom>(99);}
            if(poison==8)span.text=std::string("a\0b",3);
            if(poison==9)span.recovery=static_cast<TerminalTextRecovery>(99);
            refused(bad,policy);}
    }
    for(auto key:{Key::OriginalPath,Key::OriginalRoot,Key::Raw}){auto request=requestFor(i18n::tariffErrorLocalePacks()[0],key);TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=4096;
        for(auto &arg:request.arguments)arg.value.content.text=std::string(maximum(key,arg.name),'x');
        TerminalMessageLine line;CHECK(layoutTerminalTariffErrorMessage(request,policy,&line)&&!line.line.line.clipped);
        policy.columns=19;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalTariffErrorMessage(request,policy,128,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
        CHECK(layoutTerminalTariffErrorMessage(request,policy,&line));for(const auto &run:line.line.runs)if(!run.content.control)CHECK(line.metadata.sources[run.content.sourceSpan].empty());}
    auto request=requestFor(i18n::tariffErrorLocalePacks()[0],Key::HumanRoot);TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=19;
    request.arguments[0].value.content.text="caller supplied human description that wraps across separate rows";TerminalMessageRows rows;CHECK(wrapTerminalTariffErrorMessage(request,policy,128,&rows)&&rows.rows.size()>1);
    request.arguments[0].value.content={u8"وصف بشري","human description",false};TerminalMessageLine line;policy.columns=1024;CHECK(layoutTerminalTariffErrorMessage(request,policy,&line));
    policy.order=TerminalTextOrder::Ascii;CHECK(layoutTerminalTariffErrorMessage(request,policy,&line)&&line.line.line.text==": human description");
    request=requestFor(i18n::tariffErrorLocalePacks()[0],Key::Raw);request.language="en-US";CHECK(layoutTerminalTariffErrorMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    request.key=Key::Count;refused(request,policy);request.key=Key::Raw;request.language="en@private";refused(request,policy);request.language="en";request.literalTone=static_cast<Tone>(99);refused(request,policy);
    request=requestFor(i18n::tariffErrorLocalePacks()[0],Key::Raw);policy.columns=4097;refused(request,policy);
    CHECK(!layoutTerminalTariffErrorMessage({},policy,nullptr)&&!wrapTerminalTariffErrorMessage({},policy,1,nullptr)&&!renderTerminalTariffErrorMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalTariffErrorMessage({},policy,1,ColorLevel::None,nullptr));
    policy.columns=19;PaintedTerminalMessageRows painted;painted.rows={"previous"};CHECK(!renderWrappedTerminalTariffErrorMessage(request,policy,0,ColorLevel::None,&painted)&&painted.rows==std::vector<std::string>{"previous"});
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack whole tariff errors with separate original/human reasons and unparsed raw callback envelopes");}
