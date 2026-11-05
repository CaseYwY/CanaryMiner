// Game request/review envelopes only. Invariant private input never becomes detection or mining authority.
#include "product/terminal_settings_message.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::GameValueMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::gameValueMessageKeyCount==7&&i18n::gameValueLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,7> english={{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}:","> {input}_","Effective: {mode} (frozen)"}};
    for(std::size_t key=0;key<7;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::gameValueMessageKeyName(k))).second&&i18n::gameValueMessageKeyName(k).find("game.envelope.")==0&&i18n::gameValueMessage("en",k).text==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::gameValueLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<7;++key){const auto message=i18n::gameValueMessage(pack.id,static_cast<Key>(key));
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));}}
    CHECK(i18n::gameValueEnglish(Key::FieldValue,{{"field","Target"},{"value","fixture.exe"}})=="Target: fixture.exe");
    CHECK(i18n::gameValueEnglish(Key::FieldChange,{{"after","fixture.exe"},{"field","Target"},{"before",""}})=="Target:  -> fixture.exe");
    CHECK(i18n::gameValueEnglish(Key::ScopeChange,{{"field","Scope"},{"before","Windows session"},{"after","Linux native UID"}})=="Scope: Windows session -> Linux native UID");
    CHECK(i18n::gameValueEnglish(Key::EffectiveFrozen,{{"mode","coexist"}})=="Effective: coexist (frozen)");
    CHECK(i18n::gameValueEnglish(Key::FieldValue,{{"field","{value}"},{"value","{field}"}})=="{value}: {field}");
    for(const auto &input:std::vector<std::string>{"","qDLS","{input}",std::string(128,'x')})CHECK(i18n::gameValueEnglish(Key::EditInput,{{"input",input}})=="> "+input+"_");
    for(const auto &input:std::vector<std::string>{u8"ع","\x1b",std::string(129,'x')})CHECK(i18n::gameValueEnglish(Key::EditInput,{{"input",input}}).empty());
    CHECK(i18n::gameValueEnglish(Key::FieldChange,{{"field","x"},{"before","y"}}).empty()&&i18n::gameValueEnglish(Key::FieldValue,{{"field","x"},{"field","y"}}).empty());
    CHECK(i18n::gameValueMessageKeyName(Key::Count).empty()&&i18n::gameValueMessage("en",Key::Count).text.empty()&&i18n::gameValueEnglish(Key::Count,{}).empty());
}
bool scopeKey(Key key){return key==Key::ScopeValue||key==Key::ScopeChange;}
TerminalGameValueMessageRequest requestFor(const i18n::GameValueLocalePack &pack,Key key){
    TerminalGameValueMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;
    std::vector<std::string_view> names;CHECK(i18n::templateParameters(i18n::gameValueMessage(pack.id,key).text,&names));
    for(const auto name:names){TerminalSegment value{{name=="mode"?"coexist":name=="input"?"qDLS":name=="after"?"fixture-b.exe":"fixture-a.exe",{},true},Tone::Red};
        if(name=="field"){const auto label=scopeKey(key)?i18n::GameFieldMessageKey::Scope:i18n::GameFieldMessageKey::Target;value={{std::string(i18n::gameFieldMessage(pack.id,label).text),scopeKey(key)?"Scope":"Target"},Tone::Canary};}
        else if(scopeKey(key)){const auto label=name=="after"?i18n::GameFieldMessageKey::LinuxNativeUid:i18n::GameFieldMessageKey::WindowsSession;value={{std::string(i18n::gameFieldMessage(pack.id,label).text),name=="after"?"Linux native UID":"Windows session"},Tone::Red};}
        request.arguments.insert(request.arguments.begin(),{std::string(name),std::move(value)});}
    return request;
}
void matrix(){
    for(const auto &pack:i18n::gameValueLocalePacks())for(std::size_t key=0;key<7;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalGameValueMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:name=="field"?Tone::Canary:Tone::Red));}}
            for(const auto &arg:request.arguments)if(arg.value.content.invariant){std::string actual;for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]==arg.name)actual+=run.content.text;CHECK(actual.empty()||actual==arg.value.content.text);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalGameValueMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){const auto text=i18n::gameValueMessage(recover?"en":pack.id,request.key).text;std::string expected;std::size_t at=0;
                while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){expected+=text.substr(at);break;}expected+=text.substr(at,start-at);const auto end=text.find('}',start);const auto name=text.substr(start+1,end-start-1);
                    const auto arg=std::find_if(request.arguments.begin(),request.arguments.end(),[&](const auto &a){return a.name==name;});CHECK(arg!=request.arguments.end());const auto &span=arg->value.content;expected+=recover?(span.ascii.empty()?span.text:span.ascii):span.text;at=end+1;}
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalGameValueMessage(request,policy,128,&rows)&&renderWrappedTerminalGameValueMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    const auto request=requestFor(i18n::gameValueLocalePacks()[0],Key::EditInput);
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<12;++mode){auto bad=request;TerminalTextPolicy policy;policy.order=order;
        if(mode==0)bad.key=Key::Count;
        if(mode==1)bad.arguments.clear();
        if(mode==2)bad.arguments.push_back(bad.arguments[0]);
        if(mode==3)bad.arguments[0].name="unexpected";
        if(mode==4)bad.arguments[0].value.content.invariant=false;
        if(mode==5)bad.arguments[0].value.content.text=u8"١";
        if(mode==6)bad.arguments[0].value.content.text=std::string(129,'x');
        if(mode==7)bad.arguments[0].value.content.text="\x1b";
        if(mode==8)bad.arguments[0].value.content.ascii="substituted";
        if(mode==9)bad.language="en@private";
        if(mode==10)bad.literalTone=static_cast<Tone>(99);
        if(mode==11)bad.arguments[0].name=std::string(33,'x');
        TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
        PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
        CHECK(!layoutTerminalGameValueMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalGameValueMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalGameValueMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalGameValueMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=1024;TerminalMessageLine line;
    for(auto key:{Key::FieldValue,Key::FieldChange,Key::EffectiveFrozen}){auto bad=requestFor(i18n::gameValueLocalePacks()[0],key);for(auto &arg:bad.arguments)if(arg.name!="field")arg.value.content.invariant=false;CHECK(!layoutTerminalGameValueMessage(bad,policy,&line));}
    for(auto key:{Key::ScopeValue,Key::ScopeChange}){auto bad=requestFor(i18n::gameValueLocalePacks()[0],key);for(auto &arg:bad.arguments)if(arg.name!="field")arg.value.content.invariant=true;CHECK(!layoutTerminalGameValueMessage(bad,policy,&line));}
    for(const auto &input:std::vector<std::string>{"","0","qDLS","{input}",std::string(128,'x')}){auto sample=request;sample.arguments[0].value.content.text=input;CHECK(layoutTerminalGameValueMessage(sample,policy,&line)&&unisolate(line.line.line.text)=="> "+input+"_");}
    auto unknown=request;unknown.language="en-US";CHECK(layoutTerminalGameValueMessage(unknown,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    policy.columns=3;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalGameValueMessage(request,policy,128,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalGameValueMessage({},policy,nullptr)&&!wrapTerminalGameValueMessage({},policy,1,nullptr)&&!renderTerminalGameValueMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalGameValueMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();matrix();failures();return finishTests("sixteen-pack game envelopes with invariant requests and separate human scope descriptions");}
