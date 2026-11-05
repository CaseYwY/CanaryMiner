// Saved opaque requests and human toggle/missing descriptions, not localized prices or financial observations.
#include "product/terminal_settings_message.h"
#include "product/manual_market_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::ManualMarketValueMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::manualMarketValueMessageKeyCount==12&&i18n::manualMarketValueLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,12> english={{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}: not supplied","{field}: {before} -> not supplied","{field}: not supplied -> {after}","Edit {field}","> {input}_","on","off","not supplied"}};
    for(std::size_t key=0;key<12;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::manualMarketValueMessageKeyName(k))).second&&i18n::manualMarketValueMessageKeyName(k).find("manual_market.value.")==0&&i18n::manualMarketValueMessage("en",k).text==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::manualMarketValueLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<12;++key){const auto message=i18n::manualMarketValueMessage(pack.id,static_cast<Key>(key));
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));}}
    CHECK(i18n::manualMarketValueEnglish(Key::FieldValue,{{"value","2.5"},{"field","PRL price (USDT/PRL)"}})=="PRL price (USDT/PRL): 2.5");
    CHECK(i18n::manualMarketValueEnglish(Key::FieldChange,{{"after","0"},{"field","UTC"},{"before","1800000000"}})=="UTC: 1800000000 -> 0");
    CHECK(i18n::manualMarketValueEnglish(Key::FieldBeforeOnly,{{"before","2.5"},{"field","Price"}})=="Price: 2.5 -> not supplied");
    CHECK(i18n::manualMarketValueEnglish(Key::FieldAfterOnly,{{"field","Price"},{"after","2.5"}})=="Price: not supplied -> 2.5");
    CHECK(i18n::manualMarketValueEnglish(Key::MissingFieldValue,{{"field","Price"}})=="Price: not supplied");
    CHECK(i18n::manualMarketValueEnglish(Key::HumanFieldChange,{{"field","Display"},{"before","off"},{"after","on"}})=="Display: off -> on");
    CHECK(i18n::manualMarketValueEnglish(Key::FieldValue,{{"field","{value}"},{"value","{field}"}})=="{value}: {field}");
    for(std::size_t k=9;k<12;++k)CHECK(i18n::manualMarketValueEnglish(static_cast<Key>(k))==english[k]);
    for(const auto &input:std::vector<std::string>{"","qDLS","{input}",std::string(64,'x')})CHECK(i18n::manualMarketValueEnglish(Key::EditInput,{{"input",input}})=="> "+input+"_");
    for(const auto &input:std::vector<std::string>{u8"ع","\x1b",std::string(65,'x')})CHECK(i18n::manualMarketValueEnglish(Key::EditInput,{{"input",input}}).empty());
    CHECK(i18n::manualMarketValueEnglish(Key::FieldValue,{{"field","x"},{"value",""}}).empty()&&i18n::manualMarketValueEnglish(Key::FieldValue,{{"field","x"},{"field","y"}}).empty()&&i18n::manualMarketValueEnglish(Key::On,{{"extra","x"}}).empty());
    CHECK(i18n::manualMarketValueMessageKeyName(Key::Count).empty()&&i18n::manualMarketValueMessage("en",Key::Count).text.empty()&&i18n::manualMarketValueEnglish(Key::Count).empty());
}
void native(){
    const std::array<std::string,8> labels={{"Display enabled","PRL price (USDT/PRL)","PRL reference UTC seconds","FX enabled","FX price (USD/USDT)","FX reference UTC seconds","Validity seconds","Maximum FX skew seconds"}};
    for(bool oldPrice:{false,true})for(bool newPrice:{false,true})for(bool oldFx:{false,true})for(bool newFx:{false,true}){
        ManualMarketPreferences active,draft;active.enabled=active.fxEnabled=oldFx;draft.enabled=draft.fxEnabled=newFx;
        active.prlUsdt=active.usdtUsd=oldPrice?"2.5":"";draft.prlUsdt=draft.usdtUsd=newPrice?"3":"";
        active.prlAsOfUtc=active.fxAsOfUtc=oldPrice?1800000000:0;draft.prlAsOfUtc=draft.fxAsOfUtc=newPrice?1800000001:0;
        const auto values=[](const ManualMarketPreferences &p){return std::array<std::string,8>{{p.enabled?"on":"off",p.prlUsdt.empty()?"not supplied":p.prlUsdt,std::to_string(p.prlAsOfUtc),p.fxEnabled?"on":"off",p.usdtUsd.empty()?"not supplied":p.usdtUsd,std::to_string(p.fxAsOfUtc),std::to_string(p.validitySeconds),std::to_string(p.maxFxSkewSeconds)}};};
        ManualMarketSettingsPanel panel;panel.open(active,{});panel.draft=draft;const auto before=values(active),after=values(draft);
        const auto browse=panel.render(1024,1000,ColorLevel::None);panel.page=ManualMarketSettingsPage::Review;const auto review=panel.render(1024,1000,ColorLevel::None);
        // Includes intentionally inconsistent developer render states, never parser or financial admission.
        for(std::size_t field=0;field<8;++field){CHECK(browse.find(labels[field]+": "+after[field])!=browse.npos);
            if(before[field]!=after[field])CHECK(review.find(labels[field]+": "+before[field]+" -> "+after[field])!=review.npos);}
        CHECK(panel.active==active&&panel.draft==draft&&manualMarketJson(panel.active)==manualMarketJson(active)&&manualMarketJson(panel.draft)==manualMarketJson(draft));
    }
}
TerminalManualMarketValueMessageRequest requestFor(const i18n::ManualMarketValueLocalePack &pack,Key key){
    TerminalManualMarketValueMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;
    const bool human=key==Key::HumanFieldValue||key==Key::HumanFieldChange;
    std::vector<std::string_view> names;CHECK(i18n::templateParameters(i18n::manualMarketValueMessage(pack.id,key).text,&names));
    for(const auto name:names){TerminalSegment value{{name=="input"?"qDLS":name=="after"?"3":"2.5",{},true},Tone::Red};
        if(name=="field"){const auto label=human?i18n::ManualMarketFieldMessageKey::DisplayEnabled:i18n::ManualMarketFieldMessageKey::PrlPrice;
            value={{std::string(i18n::manualMarketFieldMessage(pack.id,label).text),human?"Display enabled":"PRL price (USDT/PRL)"},Tone::Canary};}
        else if(human){const auto state=name=="before"?Key::Off:Key::On;value={{std::string(i18n::manualMarketValueMessage(pack.id,state).text),name=="before"?"off":"on"},Tone::Red};}
        request.arguments.insert(request.arguments.begin(),{std::string(name),std::move(value)});}
    return request;
}
void matrix(){
    for(const auto &pack:i18n::manualMarketValueLocalePacks())for(std::size_t key=0;key<12;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalManualMarketValueMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:name=="field"?Tone::Canary:Tone::Red));}}
            for(const auto &arg:request.arguments)if(arg.value.content.invariant){std::string actual;for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]==arg.name)actual+=run.content.text;CHECK(actual.empty()||actual==arg.value.content.text);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalManualMarketValueMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){const auto text=i18n::manualMarketValueMessage(recover?"en":pack.id,request.key).text;std::string expected;std::size_t at=0;
                while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){expected+=text.substr(at);break;}expected+=text.substr(at,start-at);const auto end=text.find('}',start);const auto name=text.substr(start+1,end-start-1);
                    const auto arg=std::find_if(request.arguments.begin(),request.arguments.end(),[&](const auto &a){return a.name==name;});CHECK(arg!=request.arguments.end());const auto &span=arg->value.content;expected+=recover?(span.ascii.empty()?span.text:span.ascii):span.text;at=end+1;}
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalManualMarketValueMessage(request,policy,128,&rows)&&renderWrappedTerminalManualMarketValueMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void refused(const TerminalManualMarketValueMessageRequest &bad,const TerminalTextPolicy &policy){
    TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
    PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
    CHECK(!layoutTerminalManualMarketValueMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
    CHECK(!wrapTerminalManualMarketValueMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
    CHECK(!renderTerminalManualMarketValueMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
    CHECK(!renderWrappedTerminalManualMarketValueMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});
}
void failures(){
    for(const auto &pack:i18n::manualMarketValueLocalePacks())for(std::size_t key=0;key<12;++key)for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
        const auto original=requestFor(pack,static_cast<Key>(key));TerminalTextPolicy policy;policy.order=order;auto bad=original;
        bad.arguments.push_back({"extra",{{"x",{},true},Tone::Red}});refused(bad,policy);
        if(!original.arguments.empty()){bad=original;bad.arguments.pop_back();refused(bad,policy);bad=original;bad.arguments.push_back(bad.arguments[0]);refused(bad,policy);}
        for(std::size_t i=0;i<original.arguments.size();++i)for(unsigned poison=0;poison<7;++poison){bad=original;auto &span=bad.arguments[i].value.content;const auto maximum=span.invariant?64:256;
            if(poison==0)span.invariant=!span.invariant;
            if(poison==1)span.text="\x1b";
            if(poison==2)span.text=std::string(maximum+1,'x');
            if(poison==3)span.ascii=std::string(maximum+1,'x');
            if(poison==4){span.text="\xC3";span.ascii="safe";}
            if(poison==5){span.ascii="\x1b";}
            if(poison==6){if(span.invariant)span.ascii="substituted";else span.text.clear();}
            refused(bad,policy);}
    }
    auto request=requestFor(i18n::manualMarketValueLocalePacks()[0],Key::EditInput);TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=1024;TerminalMessageLine line;
    for(const auto &input:std::vector<std::string>{"","0","{input}",std::string(64,'x')}){request.arguments[0].value.content.text=input;CHECK(layoutTerminalManualMarketValueMessage(request,policy,&line)&&unisolate(line.line.line.text)=="> "+input+"_");}
    request.arguments[0].value.content.text=u8"١";refused(request,policy);request=requestFor(i18n::manualMarketValueLocalePacks()[0],Key::FieldValue);
    for(auto &arg:request.arguments)if(arg.name=="value")arg.value.content.text=std::string(38,'9');
    CHECK(layoutTerminalManualMarketValueMessage(request,policy,&line)&&!line.line.line.clipped);
    policy.columns=19;CHECK(layoutTerminalManualMarketValueMessage(request,policy,&line));std::string clipped;
    for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]=="value")clipped+=run.content.text;
    CHECK(clipped.empty());PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalManualMarketValueMessage(request,policy,128,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    policy.columns=1024;request=requestFor(i18n::manualMarketValueLocalePacks()[0],Key::MissingFieldValue);request.language="en-US";CHECK(layoutTerminalManualMarketValueMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    request.key=Key::Count;refused(request,policy);request.key=Key::MissingFieldValue;request.language="en@private";refused(request,policy);
    CHECK(!layoutTerminalManualMarketValueMessage({},policy,nullptr)&&!wrapTerminalManualMarketValueMessage({},policy,1,nullptr)&&!renderTerminalManualMarketValueMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalManualMarketValueMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack manual request/input envelopes with human toggles and literal missing prices");}
