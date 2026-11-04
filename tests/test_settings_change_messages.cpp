// Complete before/after review templates. No preference parsing, dirty detection or native translation activation.
#include "product/terminal_settings_message.h"
#include "product/terminal_number.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::SettingsChangeMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalogs(){
    CHECK(i18n::settingsChangeMessageKeyCount==16&&i18n::settingsChangeLocalePacks().size()==16);std::set<std::string> keys;
    const std::array<std::string,16> names={{"Display refresh","Terminal color","Recent events","Hide worker label","Details page","Log page","Details identity","Details pool","Details restarts","Details errors","Unclassified logs","Log timestamps","ISO UTC time","Log follow","Log level","Log records"}};
    for(std::size_t key=0;key<16;++key){const auto k=static_cast<Key>(key);const auto identity=i18n::settingsChangeMessageKeyName(k);
        CHECK(identity.find("settings.change.")==0&&keys.insert(std::string(identity)).second);
        CHECK(i18n::settingsChangeEnglish(k,"on","off")==names[key]+": on -> off");
        CHECK(i18n::settingsChangeEnglish(k,"off","on")==names[key]+": off -> on");
        CHECK(i18n::settingsChangeEnglish(k,"{after}","{before}")==names[key]+": {after} -> {before}");}
    const auto &source=i18n::settingsChangeLocalePacks()[0];
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::settingsChangeLocalePacks()[p];const auto &common=i18n::localePacks()[p];
        CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<16;++key){const auto message=i18n::settingsChangeMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl);
            CHECK(i18n::validTemplateTranslation(source.messages[key],message.text)&&i18n::templateParameters(message.text,&parameters)&&parameters==std::vector<std::string_view>({"after","before"}));}}
    CHECK(i18n::settingsChangeEnglish(Key::Refresh,"500","1000 ms")=="Display refresh: 500 -> 1000 ms");
    CHECK(i18n::settingsChangeEnglish(Key::LogLevel,"all","warnings/errors")=="Log level: all -> warnings/errors");
    CHECK(i18n::settingsChangeEnglish(Key::LogRecords,"200","25")=="Log records: 200 -> 25");
    CHECK(i18n::settingsChangeMessageKeyName(Key::Count).empty()&&i18n::settingsChangeMessage("en",Key::Count).text.empty()&&i18n::settingsChangeEnglish(Key::Count,"on","off").empty());
    for(const auto &bad:std::vector<std::string>{"",u8"开","\x1b","x\n",std::string(4097,'x')}){
        CHECK(i18n::settingsChangeEnglish(Key::Color,bad,"off").empty()&&i18n::settingsChangeEnglish(Key::Color,"off",bad).empty());}
    CHECK(i18n::settingsChangeEnglish(Key::Color,std::string(2048,'x'),std::string(2048,'y')).empty());
    const auto base=i18n::settingsChangeEnglish(Key::Color,"x","y").size();
    CHECK(i18n::settingsChangeEnglish(Key::Color,std::string(4096-base+1,'x'),"y").size()==4096);
}
TerminalSettingsChangeMessageRequest requestFor(const i18n::SettingsChangeLocalePack &pack,Key key){
    TerminalSettingsChangeMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;
    TerminalSegment before{{std::string(i18n::uiFieldMessage(pack.id,i18n::UiFieldMessageKey::On).text),"on"},Tone::Red};
    TerminalSegment after{{std::string(i18n::uiFieldMessage(pack.id,i18n::UiFieldMessageKey::Off).text),"off"},Tone::Canary};
    if(key==Key::Refresh||key==Key::LogRecords){TerminalQuantityRequest number;number.number.formattingLocale="ar-EG";number.number.digits=PresentationDigits::Locale;number.number.fractionDigits=0;
        if(key==Key::Refresh)number.label="ms";
        TerminalQuantity quantity;number.number.decimal="500";number.tone=Tone::Red;CHECK(formatTerminalQuantity(number,&quantity));before=quantity.segment;
        number.number.decimal="1000";number.tone=Tone::Canary;CHECK(formatTerminalQuantity(number,&quantity));after=quantity.segment;
    }
    if(key==Key::LogLevel){before={{std::string(i18n::uiFieldMessage(pack.id,i18n::UiFieldMessageKey::All).text),"all"},Tone::Red};
        after={{std::string(i18n::uiFieldMessage(pack.id,i18n::UiFieldMessageKey::Warnings).text),"warnings/errors"},Tone::Canary};}
    // Deliberately supplied in reverse order: named roles, not vector order, determine placement.
    request.arguments={{"after",std::move(after)},{"before",std::move(before)}};return request;
}
void checkRuns(const TerminalSettingsChangeMessageRequest &request,const TerminalMessageMetadata &metadata,const ColoredTerminalLine &line,bool recover,bool complete){
    std::string joined,before,after;std::size_t cells=0;
    for(const auto &run:line.runs){joined+=run.content.text;cells+=run.content.cells;
        if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
        else{CHECK(!run.content.padding&&run.content.sourceSpan<metadata.sources.size());const auto &name=metadata.sources[run.content.sourceSpan];
            CHECK(run.tone==(name.empty()?Tone::Green:name=="before"?Tone::Red:Tone::Canary));
            if(!name.empty()){CHECK(name=="before"||name=="after");(name=="before"?before:after)+=run.content.text;}}}
    CHECK(joined==line.line.text&&cells==line.line.cells);
    if(request.key==Key::Refresh||request.key==Key::LogRecords){for(const auto &argument:request.arguments){const auto &span=argument.value.content;const auto &expected=recover?span.ascii:span.text;
        const auto &actual=argument.name=="before"?before:after;CHECK((!complete&&actual.empty())||actual==expected);}}
}
void matrix(){
    for(const auto &pack:i18n::settingsChangeLocalePacks())for(std::size_t key=0;key<16;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalSettingsChangeMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            CHECK(line.metadata.asciiRecovery==(order==TerminalTextOrder::Ascii)&&line.metadata.backendFallback==(order!=TerminalTextOrder::Ascii&&!terminalTextBackend().unicode));
            checkRuns(request,line.metadata,line.line,recover,width==1024);CHECK(line.line.line.cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalSettingsChangeMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==line.line.line.text&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){const auto text=i18n::settingsChangeMessage(recover?"en":pack.id,request.key).text;std::string expected;std::size_t at=0;
                while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){expected+=text.substr(at);break;}expected+=text.substr(at,start-at);const auto end=text.find('}',start);const auto name=text.substr(start+1,end-start-1);
                    const auto arg=std::find_if(request.arguments.begin(),request.arguments.end(),[&](const auto &a){return a.name==name;});CHECK(arg!=request.arguments.end());const auto &span=arg->value.content;
                    expected+=recover?(span.ascii.empty()?span.text:span.ascii):span.text;at=end+1;}
                CHECK(unisolate(line.line.line.text)==expected&&!line.line.line.clipped);}
        }
    }
}
void rows(){
    for(const auto &pack:i18n::settingsChangeLocalePacks())for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
        auto request=requestFor(pack,Key::Refresh);TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
        CHECK(wrapTerminalSettingsChangeMessage(request,policy,128,&rows)&&renderWrappedTerminalSettingsChangeMessage(request,policy,128,ColorLevel::TrueColor,&painted)&&rows.rows.size()==painted.rows.size());
        std::string before,after;const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
        for(std::size_t i=0;i<rows.rows.size();++i){const auto &row=rows.rows[i];checkRuns(request,rows.metadata,row,recover,false);CHECK(!row.line.clipped&&row.line.cells<=19&&unpaint(painted.rows[i])==row.line.text);
            for(const auto &run:row.runs)if(!run.content.control&&!rows.metadata.sources[run.content.sourceSpan].empty())(rows.metadata.sources[run.content.sourceSpan]=="before"?before:after)+=run.content.text;}
        for(const auto &arg:request.arguments)CHECK((arg.name=="before"?before:after)==(recover?arg.value.content.ascii:arg.value.content.text));
    }
}
void failures(){
    const auto &pack=i18n::settingsChangeLocalePacks()[0];auto request=requestFor(pack,Key::Refresh);
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<20;++mode){auto bad=request;TerminalTextPolicy policy;policy.order=order;policy.columns=79;
        if(mode==0)bad.key=Key::Count;
        if(mode==1)bad.arguments.clear();
        if(mode==2)bad.arguments.pop_back();
        if(mode==3)bad.arguments.push_back(bad.arguments[0]);
        if(mode==4)bad.arguments[0].name="unexpected";
        if(mode==5)bad.arguments[0].value.content.text="\x1b";
        if(mode==6)bad.arguments[0].value.content.text="";
        if(mode==7)bad.arguments[1].value.content.text="";
        if(mode==8)bad.arguments[0].value.content.atom=TerminalTextAtom::None;
        if(mode==9)bad.arguments[1].value.content.atom=TerminalTextAtom::None;
        if(mode==10)bad.arguments[0].value.content.text=std::string(4097,'x');
        if(mode==11)bad.arguments[0].value.content.ascii=std::string(4097,'x');
        if(mode==12)bad.language="en@private";
        if(mode==13)bad.literalTone=static_cast<Tone>(99);
        if(mode==14)bad.arguments[0].value.tone=static_cast<Tone>(99);
        if(mode==15)bad.arguments[0].value.content.invariant=true,bad.arguments[0].value.content.text=u8"١";
        if(mode==16)bad.arguments[0].value.content.text="\xc0\xaf";
        if(mode==17)bad.arguments[0].name=std::string(33,'x');
        if(mode==18)bad.arguments.resize(9,bad.arguments[0]);
        if(mode==19)bad.arguments[0].value.content.recovery=static_cast<TerminalTextRecovery>(99);
        TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
        PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
        CHECK(!layoutTerminalSettingsChangeMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalSettingsChangeMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalSettingsChangeMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalSettingsChangeMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});
    }
    TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=1024;request.language="en-US";TerminalMessageLine line;
    CHECK(layoutTerminalSettingsChangeMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    request=requestFor(pack,Key::Color);request.arguments[0].value.content.text="{before}";request.arguments[0].value.content.ascii.clear();request.arguments[1].value.content.text="{after}";request.arguments[1].value.content.ascii.clear();
    CHECK(layoutTerminalSettingsChangeMessage(request,policy,&line)&&unisolate(line.line.line.text)=="Terminal color: {after} -> {before}");
    request=requestFor(pack,Key::Color);request.arguments[0].value=request.arguments[1].value;
    CHECK(layoutTerminalSettingsChangeMessage(request,policy,&line)); // The renderer cannot invent whether a setting is dirty.
    request=requestFor(i18n::settingsChangeLocalePacks()[1],Key::Color);request.arguments[0].value.content.ascii.clear();policy.order=TerminalTextOrder::Ascii;
    line.line.line.text="previous";CHECK(!layoutTerminalSettingsChangeMessage(request,policy,&line)&&line.line.line.text=="previous");
    request=requestFor(pack,Key::Refresh);policy.columns=3;TerminalMessageRows rows;rows.metadata.renderedLanguage="previous";PaintedTerminalMessageRows painted;painted.rows={"previous"};
    CHECK(!wrapTerminalSettingsChangeMessage(request,policy,128,&rows)&&rows.metadata.renderedLanguage=="previous");
    CHECK(!renderWrappedTerminalSettingsChangeMessage(request,policy,1,ColorLevel::None,&painted)&&painted.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalSettingsChangeMessage({},policy,nullptr)&&!wrapTerminalSettingsChangeMessage({},policy,1,nullptr)&&!renderTerminalSettingsChangeMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalSettingsChangeMessage({},policy,1,ColorLevel::None,nullptr));
}
}
int main(){catalogs();matrix();rows();failures();return finishTests("sixteen-pack complete settings changes with named before/after roles (not activation)");}
