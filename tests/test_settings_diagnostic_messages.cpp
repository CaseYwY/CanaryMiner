// Fixed diagnostic envelopes and independently admitted display values, never raw-error scrubbing/translation.
#include "product/terminal_settings_message.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::SettingsDiagnosticMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalogs(){
    CHECK(i18n::settingsDiagnosticMessageKeyCount==17&&i18n::settingsDiagnosticLocalePacks().size()==16);std::set<std::string> identities;const auto &source=i18n::settingsDiagnosticLocalePacks()[0];
    const std::array<std::string,7> prefixes={{"Runtime controls unavailable: ","Offline-market settings unavailable: ","Game defaults unavailable: ","Pool security declarations unavailable: ","Language requests unavailable: ","Electricity settings unavailable: ","Combined tariff unavailable: "}};
    for(std::size_t key=0;key<17;++key){const auto k=static_cast<Key>(key);const auto identity=i18n::settingsDiagnosticMessageKeyName(k);
        CHECK(identity.find("settings.diagnostic.")==0&&identities.insert(std::string(identity)).second);
        if(key<14&&key%2){CHECK(i18n::settingsDiagnosticEnglish(k).empty()&&i18n::settingsDiagnosticEnglishPrefix(k)==prefixes[key/2]);
            const std::string raw="synthetic %s {error}\n\x1b";CHECK(i18n::settingsDiagnosticEnglishPrefix(k)+raw==prefixes[key/2]+raw);}
        else CHECK(i18n::settingsDiagnosticEnglish(k)==source.messages[key]&&i18n::settingsDiagnosticEnglishPrefix(k).empty());
    }
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::settingsDiagnosticLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<17;++key){const auto message=i18n::settingsDiagnosticMessage(pack.id,static_cast<Key>(key));
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(source.messages[key],message.text));
            if(key==14)CHECK(message.text.find("settings")!=message.text.npos);}}
    CHECK(i18n::settingsDiagnosticEnglish(Key::RequiresTerminal)=="settings requires an interactive terminal; no mining was started");
    CHECK(i18n::settingsDiagnosticEnglish(Key::Closed)=="Settings closed. No mining was started.");
    CHECK(i18n::settingsDiagnosticEnglish(Key::EnergyHelper)=="Energy persistence unavailable; check the development helper installation.");
    CHECK(i18n::settingsDiagnosticMessageKeyName(Key::Count).empty()&&i18n::settingsDiagnosticMessage("en",Key::Count).text.empty()&&i18n::settingsDiagnosticEnglish(Key::Count).empty()&&i18n::settingsDiagnosticEnglishPrefix(Key::Count).empty());
}
TerminalSettingsDiagnosticMessageRequest requestFor(const i18n::SettingsDiagnosticLocalePack &pack,Key key){
    TerminalSettingsDiagnosticMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;
    if(i18n::settingsDiagnosticMessage(pack.id,key).text.find("{error}")!=std::string_view::npos)request.arguments={{"error",{{u8"synthetic 診断","synthetic diagnostic"},Tone::Red}}};
    return request;
}
void matrix(){
    for(const auto &pack:i18n::settingsDiagnosticLocalePacks())for(std::size_t key=0;key<17;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalSettingsDiagnosticMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            CHECK(line.metadata.asciiRecovery==(order==TerminalTextOrder::Ascii)&&line.metadata.backendFallback==(order!=TerminalTextOrder::Ascii&&!terminalTextBackend().unicode));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());CHECK(run.tone==(line.metadata.sources[run.content.sourceSpan].empty()?Tone::Green:Tone::Red));}}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalSettingsDiagnosticMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){auto expected=std::string(i18n::settingsDiagnosticMessage(recover?"en":pack.id,request.key).text);
                if(!request.arguments.empty()){const auto &span=request.arguments[0].value.content;expected.replace(expected.find("{error}"),7,recover?span.ascii:span.text);}
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalSettingsDiagnosticMessage(request,policy,128,&rows)&&renderWrappedTerminalSettingsDiagnosticMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    const auto &pack=i18n::settingsDiagnosticLocalePacks()[0];auto request=requestFor(pack,Key::RuntimeError);
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<16;++mode){auto bad=request;TerminalTextPolicy policy;policy.order=order;policy.columns=79;
        if(mode==0)bad.key=Key::Count;
        if(mode==1)bad.arguments.clear();
        if(mode==2)bad.arguments.push_back(bad.arguments[0]);
        if(mode==3)bad.arguments[0].name="unexpected";
        if(mode==4)bad.arguments[0].value.content.text="\x1b";
        if(mode==5)bad.arguments[0].value.content.text="x\n";
        if(mode==6)bad.arguments[0].value.content.text="\xc0\xaf";
        if(mode==7)bad.arguments[0].value.content.ascii="\x1b";
        if(mode==8)bad.language="en@private";
        if(mode==9)bad.literalTone=static_cast<Tone>(99);
        if(mode==10)bad.arguments[0].value.tone=static_cast<Tone>(99);
        if(mode==11)bad.arguments[0].value.content.text=std::string(4097,'x');
        if(mode==12)bad.arguments[0].value.content.ascii=std::string(4097,'x');
        if(mode==13)bad.arguments[0].name=std::string(33,'x');
        if(mode==14)bad.arguments.resize(9,bad.arguments[0]);
        if(mode==15)bad.key=Key::Closed;
        TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
        PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
        CHECK(!layoutTerminalSettingsDiagnosticMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalSettingsDiagnosticMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalSettingsDiagnosticMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalSettingsDiagnosticMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});
    }
    TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=1024;request.language="en-US";TerminalMessageLine line;
    CHECK(layoutTerminalSettingsDiagnosticMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    request=requestFor(pack,Key::RuntimeError);request.arguments[0].value.content={"fixture %s {error}"};
    CHECK(layoutTerminalSettingsDiagnosticMessage(request,policy,&line)&&unisolate(line.line.line.text)=="Runtime controls unavailable: fixture %s {error}");
    request.arguments[0].value.content={""};CHECK(layoutTerminalSettingsDiagnosticMessage(request,policy,&line)&&unisolate(line.line.line.text)=="Runtime controls unavailable: ");
    request=requestFor(pack,Key::RuntimeError);request.arguments[0].value.content.ascii.clear();policy.order=TerminalTextOrder::Ascii;line.line.line.text="previous";
    CHECK(!layoutTerminalSettingsDiagnosticMessage(request,policy,&line)&&line.line.line.text=="previous");
    request=requestFor(pack,Key::RequiresTerminal);policy.columns=3;PaintedTerminalMessageRows painted;painted.rows={"previous"};
    CHECK(!renderWrappedTerminalSettingsDiagnosticMessage(request,policy,1,ColorLevel::None,&painted)&&painted.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalSettingsDiagnosticMessage({},policy,nullptr)&&!wrapTerminalSettingsDiagnosticMessage({},policy,1,nullptr)&&!renderTerminalSettingsDiagnosticMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalSettingsDiagnosticMessage({},policy,1,ColorLevel::None,nullptr));
}
}
int main(){catalogs();matrix();failures();return finishTests("sixteen-pack fixed settings diagnostic envelopes with independent safe values");}
