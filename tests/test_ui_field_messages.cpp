// Root display controls only. Translation is not preference parsing, raw-log classification or activation.
#include "product/terminal_settings_message.h"
#include "product/terminal_number.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::uiFieldMessageKeyCount==25&&i18n::uiFieldLocalePacks().size()==16);std::set<std::string> keys;
    for(std::size_t key=0;key<25;++key)CHECK(keys.insert(std::string(i18n::uiFieldMessageKeyName(static_cast<i18n::UiFieldMessageKey>(key)))).second);
    const auto &source=i18n::uiFieldLocalePacks()[0];
    for(std::size_t key=0;key<25;++key)if(source.messages[key].find("{state}")!=std::string_view::npos)for(const auto *state:{"x"," "}){auto expected=std::string(source.messages[key]);expected.replace(expected.find("{state}"),7,state);
        CHECK(i18n::uiFieldEnglish(static_cast<i18n::UiFieldMessageKey>(key),state)==expected);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::uiFieldLocalePacks()[p];CHECK(pack.id==i18n::localePacks()[p].id&&pack.review==i18n::localePacks()[p].review&&pack.rtl==i18n::localePacks()[p].rtl);
        for(std::size_t key=0;key<25;++key){const auto message=i18n::uiFieldMessage(pack.id,static_cast<i18n::UiFieldMessageKey>(key));
            CHECK(!message.fallback&&message.locale==pack.id&&message.review==pack.review&&i18n::validTemplateTranslation(source.messages[key],message.text));
            if(key==static_cast<std::size_t>(i18n::UiFieldMessageKey::IntensityNotice))CHECK(message.text.find("NO_COLOR")!=message.text.npos);}}
    using Key=i18n::UiFieldMessageKey;
    CHECK(i18n::uiFieldEnglish(Key::Refresh,"500 ms")=="Display refresh    < 500 ms >");
    CHECK(i18n::uiFieldEnglish(Key::Color,"x")=="Terminal color     [x]"&&i18n::uiFieldEnglish(Key::Events," ")=="Recent events      [ ]");
    CHECK(i18n::uiFieldEnglish(Key::On)=="on"&&i18n::uiFieldEnglish(Key::Off)=="off"&&i18n::uiFieldEnglish(Key::Warnings)=="warnings/errors");
    CHECK(i18n::uiFieldEnglish(Key::TimeFormat,"ISO UTC")=="Time format < ISO UTC >");
    CHECK(i18n::uiFieldEnglish(Key::Color,"xx").empty()&&i18n::uiFieldEnglish(Key::On,"x").empty()&&i18n::uiFieldEnglish(Key::Color).empty());
    CHECK(i18n::uiFieldEnglish(Key::Refresh,u8"١ ms").empty()&&i18n::uiFieldEnglish(Key::Severity,"\x1b").empty()&&i18n::uiFieldEnglish(Key::Refresh,std::string(4096,'x')).empty());
    CHECK(i18n::uiFieldMessageKeyName(Key::Count).empty()&&i18n::uiFieldMessage("en",Key::Count).text.empty());
}
TerminalUiFieldMessageRequest requestFor(const i18n::UiFieldLocalePack &pack,i18n::UiFieldMessageKey key){
    TerminalUiFieldMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;
    const auto text=i18n::uiFieldMessage(pack.id,key).text;
    if(text.find("{state}")!=text.npos)request.arguments={{"state",{{"x",{},true},Tone::Canary}}};
    if(text.find("{value}")!=text.npos){TerminalSegment value{{"25",{},false,TerminalTextAtom::LeftToRight},Tone::Canary};
        if(key==i18n::UiFieldMessageKey::Refresh){TerminalQuantityRequest number;number.number.decimal="500";number.number.fractionDigits=0;number.number.formattingLocale="ar-EG";number.number.digits=PresentationDigits::Locale;number.label="ms";number.tone=Tone::Canary;TerminalQuantity quantity;
            CHECK(formatTerminalQuantity(number,&quantity));value=quantity.segment;}
        if(key==i18n::UiFieldMessageKey::TimeFormat)value={{"ISO UTC",{},true},Tone::Canary};
        if(key==i18n::UiFieldMessageKey::Severity)value={{std::string(i18n::uiFieldMessage(pack.id,i18n::UiFieldMessageKey::Warnings).text),"warnings/errors",false},Tone::Canary};
        request.arguments={{"value",std::move(value)}};}
    return request;
}
void matrix(){
    for(const auto &pack:i18n::uiFieldLocalePacks())for(std::size_t key=0;key<25;++key){const auto request=requestFor(pack,static_cast<i18n::UiFieldMessageKey>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalUiFieldMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined,argument;std::size_t cells=0;
            for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(run.content.sourceSpan<line.metadata.sources.size()&&!run.content.padding);const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:Tone::Canary));if(!name.empty())argument+=run.content.text;}}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalUiFieldMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined);
            if(!request.arguments.empty()&&request.key==i18n::UiFieldMessageKey::Refresh){const auto &value=request.arguments[0].value.content;const auto &expected=recover?value.ascii:value.text;
                CHECK(argument.empty()||argument==expected);}
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){auto expected=std::string(i18n::uiFieldMessage(recover?"en":pack.id,request.key).text);
                if(!request.arguments.empty()){const auto &arg=request.arguments[0];const auto &value=arg.value.content;const auto marker="{"+arg.name+"}";expected.replace(expected.find(marker),marker.size(),recover?(value.ascii.empty()?value.text:value.ascii):value.text);}
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
    }
}
void boundaries(){
    const auto &pack=i18n::uiFieldLocalePacks()[0];auto request=requestFor(pack,i18n::UiFieldMessageKey::Refresh);TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=19;
    TerminalMessageRows rows;PaintedTerminalMessageRows painted;CHECK(wrapTerminalUiFieldMessage(request,policy,64,&rows)&&renderWrappedTerminalUiFieldMessage(request,policy,64,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
    for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]);
    for(unsigned mode=0;mode<10;++mode){auto bad=request;
        if(mode==0)bad.key=i18n::UiFieldMessageKey::Count;
        if(mode==1)bad.arguments.clear();
        if(mode==2)bad.arguments.push_back(bad.arguments[0]);
        if(mode==3)bad.arguments[0].name="unexpected";
        if(mode==4)bad.arguments[0].value.content.atom=TerminalTextAtom::None;
        if(mode==5){bad=requestFor(pack,i18n::UiFieldMessageKey::Color);bad.arguments[0].value.content.invariant=false;}
        if(mode==6){bad=requestFor(pack,i18n::UiFieldMessageKey::Color);bad.arguments[0].value.content.text="xx";}
        if(mode==7){bad=requestFor(pack,i18n::UiFieldMessageKey::TimeFormat);bad.arguments[0].value.content.text="system local";}
        if(mode==8)bad.language="en@private";
        if(mode==9)bad.arguments[0].value.content.text="\x1b";
        TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";CHECK(!layoutTerminalUiFieldMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");}
    request.language="en-US";TerminalMessageLine line;CHECK(layoutTerminalUiFieldMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview);
    policy.columns=3;rows.metadata.renderedLanguage="previous";CHECK(!wrapTerminalUiFieldMessage(request,policy,64,&rows)&&rows.metadata.renderedLanguage=="previous");
    painted.rows={"previous"};CHECK(!renderWrappedTerminalUiFieldMessage(request,policy,1,ColorLevel::None,&painted)&&painted.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalUiFieldMessage({},policy,nullptr)&&!wrapTerminalUiFieldMessage({},policy,1,nullptr)&&!renderTerminalUiFieldMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalUiFieldMessage({},policy,1,ColorLevel::None,nullptr));
}
}
int main(){catalog();matrix();boundaries();return finishTests("sixteen-pack display/details/log field extraction with typed atomic values (not activation)");}
