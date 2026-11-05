// Complete current-reference context rows only; opaque strings do not authenticate prices or UTC instants.
#include "product/terminal_settings_message.h"
#include "product/manual_market_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::ManualMarketContextMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::manualMarketContextMessageKeyCount==7&&i18n::manualMarketContextLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,7> english={{"Current display: {state}","Current PRL input: {price} USDT/PRL | {stamp}","Current PRL input: {price} USDT/PRL | not supplied","Current PRL input: not supplied USDT/PRL | {stamp}","Current PRL input: not supplied USDT/PRL | not supplied","UTC: {stamp} | N explicitly stamps NOW.","UTC: not supplied | N explicitly stamps NOW."}};
    for(std::size_t key=0;key<7;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::manualMarketContextMessageKeyName(k))).second&&i18n::manualMarketContextMessageKeyName(k).find("manual_market.context.")==0&&i18n::manualMarketContextMessage("en",k).text==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::manualMarketContextLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<7;++key){const auto message=i18n::manualMarketContextMessage(pack.id,static_cast<Key>(key));CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            for(const auto *token:{"PRL","USDT/PRL","UTC"," N ","NOW"})if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::manualMarketContextEnglish(Key::CurrentPrlBoth,{{"stamp","2027-01-15T08:00:00Z"},{"price","2.5"}})=="Current PRL input: 2.5 USDT/PRL | 2027-01-15T08:00:00Z");
    CHECK(i18n::manualMarketContextEnglish(Key::CurrentPrlPrice,{{"price","2.5"}})=="Current PRL input: 2.5 USDT/PRL | not supplied");
    CHECK(i18n::manualMarketContextEnglish(Key::CurrentPrlStamp,{{"stamp","[invalid UTC]"}})=="Current PRL input: not supplied USDT/PRL | [invalid UTC]"); // opaque original error is not validated time
    CHECK(i18n::manualMarketContextEnglish(Key::CurrentPrlUnset)==english[4]&&i18n::manualMarketContextEnglish(Key::UtcUnset)==english[6]);
    CHECK(i18n::manualMarketContextEnglish(Key::CurrentPrlBoth,{{"price","{stamp}"},{"stamp","{price}"}})=="Current PRL input: {stamp} USDT/PRL | {price}");
    for(const auto &input:std::vector<std::string>{"",u8"ع","\x1b",std::string(65,'x')})CHECK(i18n::manualMarketContextEnglish(Key::UtcStamp,{{"stamp",input}}).empty());
    CHECK(!i18n::manualMarketContextEnglish(Key::UtcStamp,{{"stamp",std::string(64,'x')}}).empty());
    CHECK(i18n::manualMarketContextEnglish(Key::CurrentPrlBoth,{{"price","x"}}).empty()&&i18n::manualMarketContextEnglish(Key::CurrentPrlBoth,{{"price","x"},{"price","y"}}).empty()&&i18n::manualMarketContextEnglish(Key::UtcUnset,{{"stamp","unused"}}).empty());
    CHECK(i18n::manualMarketContextMessageKeyName(Key::Count).empty()&&i18n::manualMarketContextMessage("en",Key::Count).text.empty()&&i18n::manualMarketContextEnglish(Key::Count).empty());
}
void native(){
    for(bool price:{false,true})for(bool time:{false,true})for(bool enabled:{false,true}){
        ManualMarketPreferences effective;effective.enabled=enabled;effective.prlUsdt=price?"2.5":"";effective.prlAsOfUtc=time?1800000000:0;effective.validitySeconds=60;
        ManualMarketSettingsPanel panel;panel.open({},{true,false,effective});panel.field=2;panel.draft.prlAsOfUtc=effective.prlAsOfUtc;
        const auto stamp=time?utcIsoClock(static_cast<time_t>(1800000000)):"not supplied";
        const auto original=manualMarketJson(panel.draft),current=manualMarketJson(panel.context.effective);const auto frame=panel.render(1024,1000,ColorLevel::None);
        // Includes inconsistent developer render states, never reference/price/clock admission.
        const auto expected=std::string("Current PRL input: ")+(price?"2.5":"not supplied")+" USDT/PRL | "+stamp;
        CHECK(frame.find(expected)!=frame.npos&&frame.find(std::string("Current display: ")+(enabled?"on":"off"))!=frame.npos&&frame.find("UTC: "+stamp+" | N explicitly stamps NOW.")!=frame.npos);
        panel.context.cliDisabled=true;const auto suppressed=panel.render(1024,1000,ColorLevel::None);
        CHECK(suppressed.find("Current display: OFF (--no-market CLI override)")!=suppressed.npos&&suppressed.find("Current PRL input:")==suppressed.npos);
        CHECK(manualMarketJson(panel.draft)==original&&manualMarketJson(panel.context.effective)==current);
    }
}
TerminalManualMarketContextMessageRequest requestFor(const i18n::ManualMarketContextLocalePack &pack,Key key){
    TerminalManualMarketContextMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;
    std::vector<std::string_view> names;CHECK(i18n::templateParameters(i18n::manualMarketContextMessage(pack.id,key).text,&names));
    for(const auto name:names){TerminalSegment value{{name=="price"?"2.5":"2027-01-15T08:00:00Z",{},true},Tone::Red};
        if(name=="state")value={{std::string(i18n::manualMarketValueMessage(pack.id,i18n::ManualMarketValueMessageKey::On).text),"on"},Tone::Canary};
        request.arguments.insert(request.arguments.begin(),{std::string(name),std::move(value)});}
    return request;
}
void matrix(){
    for(const auto &pack:i18n::manualMarketContextLocalePacks())for(std::size_t key=0;key<7;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalManualMarketContextMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:name=="state"?Tone::Canary:Tone::Red));}}
            for(const auto &arg:request.arguments)if(arg.value.content.invariant){std::string actual;for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]==arg.name)actual+=run.content.text;CHECK(actual.empty()||actual==arg.value.content.text);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalManualMarketContextMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){const auto text=i18n::manualMarketContextMessage(recover?"en":pack.id,request.key).text;std::string expected;std::size_t at=0;
                while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){expected+=text.substr(at);break;}expected+=text.substr(at,start-at);const auto end=text.find('}',start);const auto name=text.substr(start+1,end-start-1);
                    const auto arg=std::find_if(request.arguments.begin(),request.arguments.end(),[&](const auto &a){return a.name==name;});CHECK(arg!=request.arguments.end());const auto &span=arg->value.content;expected+=recover?(span.ascii.empty()?span.text:span.ascii):span.text;at=end+1;}
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=29;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalManualMarketContextMessage(request,policy,128,&rows)&&renderWrappedTerminalManualMarketContextMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=29);}
    }
}
void refused(const TerminalManualMarketContextMessageRequest &bad,const TerminalTextPolicy &policy){
    TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
    PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
    CHECK(!layoutTerminalManualMarketContextMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
    CHECK(!wrapTerminalManualMarketContextMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
    CHECK(!renderTerminalManualMarketContextMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
    CHECK(!renderWrappedTerminalManualMarketContextMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});
}
void failures(){
    for(const auto &pack:i18n::manualMarketContextLocalePacks())for(std::size_t key=0;key<7;++key)for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
        const auto original=requestFor(pack,static_cast<Key>(key));TerminalTextPolicy policy;policy.order=order;auto bad=original;
        bad.arguments.push_back({"extra",{{"x",{},true},Tone::Red}});refused(bad,policy);
        if(!original.arguments.empty()){bad=original;bad.arguments.pop_back();refused(bad,policy);bad=original;bad.arguments.push_back(bad.arguments[0]);refused(bad,policy);}
        for(std::size_t i=0;i<original.arguments.size();++i)for(unsigned poison=0;poison<8;++poison){bad=original;auto &span=bad.arguments[i].value.content;const auto maximum=span.invariant?64:256;
            if(poison==0)span.invariant=!span.invariant;
            if(poison==1)span.text.clear();
            if(poison==2)span.text="\x1b";
            if(poison==3)span.text=std::string(maximum+1,'x');
            if(poison==4)span.ascii=std::string(maximum+1,'x');
            if(poison==5){span.text="\xC3";span.ascii="safe";}
            if(poison==6)span.ascii="\x1b";
            if(poison==7){if(span.invariant)span.ascii="substituted";else span.text.clear();}
            refused(bad,policy);}
    }
    auto request=requestFor(i18n::manualMarketContextLocalePacks()[0],Key::UtcStamp);TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=1024;TerminalMessageLine line;
    for(const auto &text:std::vector<std::string>{"[invalid UTC]","{stamp}",std::string(64,'x')}){request.arguments[0].value.content.text=text;CHECK(layoutTerminalManualMarketContextMessage(request,policy,&line)&&unisolate(line.line.line.text)=="UTC: "+text+" | N explicitly stamps NOW.");}
    request.arguments[0].value.content.text=u8"١";refused(request,policy);request.arguments[0].value.content.text=std::string(64,'x');policy.columns=19;
    PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalManualMarketContextMessage(request,policy,128,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    policy.columns=1024;request=requestFor(i18n::manualMarketContextLocalePacks()[0],Key::UtcUnset);request.language="en-US";CHECK(layoutTerminalManualMarketContextMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    request.key=Key::Count;refused(request,policy);request.key=Key::UtcUnset;request.language="en@private";refused(request,policy);request.language="en";request.literalTone=static_cast<Tone>(99);refused(request,policy);
    CHECK(!layoutTerminalManualMarketContextMessage({},policy,nullptr)&&!wrapTerminalManualMarketContextMessage({},policy,1,nullptr)&&!renderTerminalManualMarketContextMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalManualMarketContextMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack complete manual context envelopes without invented price or UTC facts");}
