// Runtime field text only. Atomic display quantities do not parse or authorize host waits/history collection.
#include "product/terminal_settings_message.h"
#include "product/terminal_number.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::RuntimeFieldMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalogs(){
    CHECK(i18n::runtimeFieldMessageKeyCount==13&&i18n::runtimeFieldLocalePacks().size()==16);std::set<std::string> identities;const auto &source=i18n::runtimeFieldLocalePacks()[0];
    for(std::size_t key=0;key<13;++key){const auto k=static_cast<Key>(key);const auto identity=i18n::runtimeFieldMessageKeyName(k);CHECK(identity.find("runtime.")==0&&identities.insert(std::string(identity)).second);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::runtimeFieldLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<13;++key){const auto message=i18n::runtimeFieldMessage(pack.id,static_cast<Key>(key));
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(source.messages[key],message.text));
            for(const auto *token:{"Enter","Space","CLI","--no-history"})if(source.messages[key].find(token)!=source.messages[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::runtimeFieldEnglish(Key::WaitConfigured,{{"wait","0 ms"}})=="Configured host wait: 0 ms [Enter to edit]");
    CHECK(i18n::runtimeFieldEnglish(Key::HistoryConfigured,{{"history","off"}})=="Configured history collection: off [Space to toggle]");
    CHECK(i18n::runtimeFieldEnglish(Key::CurrentWait,{{"source","CLI override"},{"wait","0 ms"}})=="Current host wait: 0 ms (CLI override)");
    CHECK(i18n::runtimeFieldEnglish(Key::WaitChange,{{"before","0"},{"after","1000 ms"}})=="Host wait: 0 -> 1000 ms");
    CHECK(i18n::runtimeFieldEnglish(Key::NextSameFlags,{{"wait","0 ms"},{"history","off"}})=="Next start with same flags: wait 0 ms; history off");
    CHECK(i18n::runtimeFieldEnglish(Key::CurrentWait,{{"wait","{source}"},{"source","{wait}"}})=="Current host wait: {source} ({wait})");
    CHECK(i18n::runtimeFieldEnglish(Key::WaitCompact).empty()&&i18n::runtimeFieldEnglish(Key::SourceCli,{{"wait","0 ms"}}).empty());
    CHECK(i18n::runtimeFieldEnglish(Key::CurrentWait,{{"wait","0 ms"},{"wait","0 ms"}}).empty());
    CHECK(i18n::runtimeFieldEnglish(Key::WaitCompact,{{"wait",u8"١ ms"}}).empty()&&i18n::runtimeFieldEnglish(Key::WaitCompact,{{"wait","\x1b"}}).empty());
    CHECK(i18n::runtimeFieldEnglish(Key::WaitCompact,{{"wait",std::string(4090,'x')}}).size()==4096&&i18n::runtimeFieldEnglish(Key::WaitCompact,{{"wait",std::string(4091,'x')}}).empty());
    CHECK(i18n::runtimeFieldMessageKeyName(Key::Count).empty()&&i18n::runtimeFieldMessage("en",Key::Count).text.empty()&&i18n::runtimeFieldEnglish(Key::Count).empty());
}
bool quantityRole(Key key,const std::string &name){return name=="wait"||(key==Key::WaitChange&&(name=="before"||name=="after"));}
TerminalRuntimeFieldMessageRequest requestFor(const i18n::RuntimeFieldLocalePack &pack,Key key){
    TerminalRuntimeFieldMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;std::vector<std::string_view> parameters;
    CHECK(i18n::templateParameters(i18n::runtimeFieldMessage(pack.id,key).text,&parameters));
    for(const auto name:parameters){TerminalSegment value{{std::string(i18n::uiFieldMessage(pack.id,i18n::UiFieldMessageKey::On).text),"on"},Tone::Canary};
        if(quantityRole(key,std::string(name))){TerminalQuantityRequest number;number.number.decimal=name=="after"?"1000":"0";number.number.formattingLocale="ar-EG";number.number.digits=PresentationDigits::Locale;number.number.fractionDigits=0;number.label="ms";number.tone=Tone::Red;TerminalQuantity quantity;CHECK(formatTerminalQuantity(number,&quantity));value=quantity.segment;}
        if(name=="source")value={{std::string(i18n::runtimeFieldMessage(pack.id,Key::SourceCli).text),"CLI override"},Tone::Canary};
        if(name=="activation")value={{"next explicit start; no automatic restart",{},false},Tone::Canary};
        request.arguments.insert(request.arguments.begin(),{std::string(name),std::move(value)});}
    return request;
}
void matrix(){
    for(const auto &pack:i18n::runtimeFieldLocalePacks())for(std::size_t key=0;key<13;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalRuntimeFieldMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:quantityRole(request.key,name)?Tone::Red:Tone::Canary));}}
            for(const auto &arg:request.arguments)if(quantityRole(request.key,arg.name)){std::string actual;for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]==arg.name)actual+=run.content.text;
                const auto &span=arg.value.content;CHECK((width<1024&&actual.empty())||actual==(recover?span.ascii:span.text));}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalRuntimeFieldMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){const auto text=i18n::runtimeFieldMessage(recover?"en":pack.id,request.key).text;std::string expected;std::size_t at=0;
                while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){expected+=text.substr(at);break;}expected+=text.substr(at,start-at);const auto end=text.find('}',start);const auto name=text.substr(start+1,end-start-1);
                    const auto arg=std::find_if(request.arguments.begin(),request.arguments.end(),[&](const auto &a){return a.name==name;});CHECK(arg!=request.arguments.end());const auto &span=arg->value.content;expected+=recover?(span.ascii.empty()?span.text:span.ascii):span.text;at=end+1;}
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalRuntimeFieldMessage(request,policy,128,&rows)&&renderWrappedTerminalRuntimeFieldMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    const auto request=requestFor(i18n::runtimeFieldLocalePacks()[0],Key::WaitCompact);
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<12;++mode){auto bad=request;TerminalTextPolicy policy;policy.order=order;
        if(mode==0)bad.key=Key::Count;
        if(mode==1)bad.arguments.clear();
        if(mode==2)bad.arguments.push_back(bad.arguments[0]);
        if(mode==3)bad.arguments[0].name="unexpected";
        if(mode==4)bad.arguments[0].value.content.atom=TerminalTextAtom::None;
        if(mode==5)bad.arguments[0].value.content.text="\x1b";
        if(mode==6)bad.language="en@private";
        if(mode==7)bad.literalTone=static_cast<Tone>(99);
        if(mode==8)bad.arguments[0].value.content.text=std::string(4097,'x');
        if(mode==9)bad.arguments[0].value.content.ascii=std::string(4097,'x');
        if(mode==10)bad.arguments[0].name=std::string(33,'x');
        if(mode==11)bad.key=Key::SourceCli;
        TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
        PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
        CHECK(!layoutTerminalRuntimeFieldMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalRuntimeFieldMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalRuntimeFieldMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalRuntimeFieldMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    auto unknown=request;unknown.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalRuntimeFieldMessage(unknown,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    auto change=requestFor(i18n::runtimeFieldLocalePacks()[0],Key::WaitChange);for(std::size_t i=0;i<2;++i){auto bad=change;bad.arguments[i].value.content.atom=TerminalTextAtom::None;CHECK(!layoutTerminalRuntimeFieldMessage(bad,policy,&line));}
    policy.columns=3;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalRuntimeFieldMessage(change,policy,128,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalRuntimeFieldMessage({},policy,nullptr)&&!wrapTerminalRuntimeFieldMessage({},policy,1,nullptr)&&!renderTerminalRuntimeFieldMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalRuntimeFieldMessage({},policy,1,ColorLevel::None,nullptr));
}
}
int main(){catalogs();matrix();failures();return finishTests("sixteen-pack runtime field envelopes with atomic explicit wait quantities (not activation)");}
