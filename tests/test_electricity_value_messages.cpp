// Original declaration/input bytes and human states only; never numeric, sensor, utility or billing qualification.
#include "product/terminal_settings_message.h"
#include "product/electricity_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::ElectricityValueMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::electricityValueMessageKeyCount==14&&i18n::electricityValueLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,14> english={{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}: not supplied","{field}: {before} -> not supplied","{field}: not supplied -> {after}","Edit {field}","> {input}_","on","off","not supplied","fixed STANDARD (no DST)","explicit UTC"}};
    for(std::size_t key=0;key<14;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::electricityValueMessageKeyName(k))).second&&i18n::electricityValueMessageKeyName(k).find("electricity.value.")==0&&i18n::electricityValueMessage("en",k).text==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::electricityValueLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<14;++key){const auto message=i18n::electricityValueMessage(pack.id,static_cast<Key>(key));CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            for(const auto *token:{"STANDARD","DST","UTC"})if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::electricityValueEnglish(Key::FieldValue,{{"value","-2.5"},{"field","Flat rate per kWh"}})=="Flat rate per kWh: -2.5");
    CHECK(i18n::electricityValueEnglish(Key::FieldChange,{{"after","0"},{"field","Rate"},{"before","-2.5"}})=="Rate: -2.5 -> 0");
    CHECK(i18n::electricityValueEnglish(Key::FieldBeforeOnly,{{"before","0"},{"field","Rate"}})=="Rate: 0 -> not supplied");
    CHECK(i18n::electricityValueEnglish(Key::FieldAfterOnly,{{"field","Rate"},{"after","0"}})=="Rate: not supplied -> 0");
    CHECK(i18n::electricityValueEnglish(Key::MissingFieldValue,{{"field","Supplier"}})=="Supplier: not supplied");
    CHECK(i18n::electricityValueEnglish(Key::HumanFieldChange,{{"field","Clock"},{"before","explicit UTC"},{"after","fixed STANDARD (no DST)"}})=="Clock: explicit UTC -> fixed STANDARD (no DST)");
    CHECK(i18n::electricityValueEnglish(Key::FieldValue,{{"field","{value}"},{"value","{field}"}})=="{value}: {field}");
    for(std::size_t k=9;k<14;++k)CHECK(i18n::electricityValueEnglish(static_cast<Key>(k))==english[k]);
    for(const auto &input:std::vector<std::string>{"","qDLS","{input}",std::string(64,'x')})CHECK(i18n::electricityValueEnglish(Key::EditInput,{{"input",input}})=="> "+input+"_");
    for(const auto &input:std::vector<std::string>{u8"ع","\x1b",std::string(65,'x')})CHECK(i18n::electricityValueEnglish(Key::EditInput,{{"input",input}}).empty());
    CHECK(i18n::electricityValueEnglish(Key::FieldValue,{{"field","x"},{"value",""}}).empty()&&i18n::electricityValueEnglish(Key::FieldValue,{{"field","x"},{"field","y"}}).empty()&&i18n::electricityValueEnglish(Key::On,{{"extra","x"}}).empty());
    CHECK(i18n::electricityValueMessageKeyName(Key::Count).empty()&&i18n::electricityValueMessage("en",Key::Count).text.empty()&&i18n::electricityValueEnglish(Key::Count).empty());
}
ElectricityPreferences declaration(unsigned variant){ElectricityPreferences p;
    if(variant){p.collectEnergy=p.tariffEnabled=p.exclusionsAcknowledged=true;p.country=p.region=p.supplier=p.product=p.version="synthetic";p.currency="USD";p.ratePerKwh=variant==1?"0":"-2.5";p.effectiveFromUtc=1800000000;p.effectiveThroughUtc=1800001000;p.confirmedUtc=1800000000;}
    if(variant==2){p.fixedStandardTime=true;p.fixedOffsetSeconds=-64800;p.recordIntervalMs=p.maximumGapMs=60000;p.supplier=std::string(64,'x');p.version="{before}{after}";}
    if(variant==3){p.showEnergy=false;p.country=p.region=p.supplier=p.product=p.version="not supplied";p.ratePerKwh="-0.000000000000000001";}
    return p;
}
std::array<std::string,17> oldValues(const ElectricityPreferences &p){const auto on=[](bool b){return b?"on":"off";};const auto supplied=[](const std::string &s){return s.empty()?"not supplied":s;};
    return {{on(p.collectEnergy),on(p.showEnergy),std::to_string(p.recordIntervalMs),std::to_string(p.maximumGapMs),on(p.tariffEnabled),on(p.exclusionsAcknowledged),supplied(p.country),supplied(p.region),supplied(p.supplier),supplied(p.product),supplied(p.version),supplied(p.currency),supplied(p.ratePerKwh),p.fixedStandardTime?"fixed STANDARD (no DST)":"explicit UTC",std::to_string(p.fixedOffsetSeconds),std::to_string(p.effectiveFromUtc),std::to_string(p.effectiveThroughUtc)}};
}
void native(){
    const std::array<std::string,17> labels={{"Collect session board energy","Show energy section","Record selection ms","Maximum hold gap ms","Use declared flat energy rate","Energy-only exclusions acknowledged","Country/jurisdiction","Region/territory","Supplier","Product/plan","Effective version key","Currency identifier","Flat rate per kWh","Reporting clock","Fixed standard offset seconds","Effective start UTC seconds","Effective end UTC seconds (exclusive)"}};
    for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b){const auto active=declaration(a),draft=declaration(b);const auto before=oldValues(active),after=oldValues(draft);ElectricitySettingsPanel panel;panel.open(active,{});panel.draft=draft;
        // Includes explicit developer-only render states; none of these comparisons admits a preference or source fact.
        for(unsigned field=0;field<17;++field){panel.field=field;const auto browse=panel.render(1024,1000,ColorLevel::None);CHECK(browse.find("> "+labels[field]+": "+after[field])!=browse.npos);}
        panel.page=ElectricitySettingsPage::Review;const auto review=panel.render(1024,1000,ColorLevel::None);
        for(unsigned field=0;field<17;++field){const auto row=labels[field]+": "+before[field]+" -> "+after[field];CHECK((review.find(row)!=review.npos)==(before[field]!=after[field]));}
        CHECK(panel.active==active&&panel.draft==draft&&electricityPreferencesJson(panel.active)==electricityPreferencesJson(active)&&electricityPreferencesJson(panel.draft)==electricityPreferencesJson(draft));
    }
}
TerminalElectricityValueMessageRequest requestFor(const i18n::ElectricityValueLocalePack &pack,Key key){
    TerminalElectricityValueMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;
    const bool human=key==Key::HumanFieldValue||key==Key::HumanFieldChange;
    std::vector<std::string_view> names;CHECK(i18n::templateParameters(i18n::electricityValueMessage(pack.id,key).text,&names));
    for(const auto name:names){TerminalSegment value{{name=="input"?"qDLS":name=="after"?"0":"-2.5",{},true},Tone::Red};
        if(name=="field"){const auto label=human?i18n::ElectricityFieldMessageKey::Clock:i18n::ElectricityFieldMessageKey::Rate;
            value={{std::string(i18n::electricityFieldMessage(pack.id,label).text),human?"Reporting clock":"Flat rate per kWh"},Tone::Canary};}
        else if(human){const auto state=name=="before"?Key::ExplicitUtc:Key::FixedClock;value={{std::string(i18n::electricityValueMessage(pack.id,state).text),name=="before"?"explicit UTC":"fixed STANDARD (no DST)"},Tone::Red};}
        request.arguments.insert(request.arguments.begin(),{std::string(name),std::move(value)});}
    return request;
}
void matrix(){
    for(const auto &pack:i18n::electricityValueLocalePacks())for(std::size_t key=0;key<14;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalElectricityValueMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:name=="field"?Tone::Canary:Tone::Red));}}
            for(const auto &arg:request.arguments)if(arg.value.content.invariant){std::string actual;for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]==arg.name)actual+=run.content.text;CHECK(actual.empty()||actual==arg.value.content.text);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalElectricityValueMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){const auto text=i18n::electricityValueMessage(recover?"en":pack.id,request.key).text;std::string expected;std::size_t at=0;
                while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){expected+=text.substr(at);break;}expected+=text.substr(at,start-at);const auto end=text.find('}',start);const auto name=text.substr(start+1,end-start-1);
                    const auto arg=std::find_if(request.arguments.begin(),request.arguments.end(),[&](const auto &a){return a.name==name;});CHECK(arg!=request.arguments.end());const auto &span=arg->value.content;expected+=recover?(span.ascii.empty()?span.text:span.ascii):span.text;at=end+1;}
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=29;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalElectricityValueMessage(request,policy,128,&rows)&&renderWrappedTerminalElectricityValueMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=29);}
    }
}
void refused(const TerminalElectricityValueMessageRequest &bad,const TerminalTextPolicy &policy){
    TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
    PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
    CHECK(!layoutTerminalElectricityValueMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
    CHECK(!wrapTerminalElectricityValueMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
    CHECK(!renderTerminalElectricityValueMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
    CHECK(!renderWrappedTerminalElectricityValueMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});
}
void failures(){
    for(const auto &pack:i18n::electricityValueLocalePacks())for(std::size_t key=0;key<14;++key)for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
        const auto original=requestFor(pack,static_cast<Key>(key));TerminalTextPolicy policy;policy.order=order;auto bad=original;
        bad.arguments.push_back({"extra",{{"x",{},true},Tone::Red}});refused(bad,policy);
        if(!original.arguments.empty()){bad=original;bad.arguments.pop_back();refused(bad,policy);bad=original;bad.arguments.push_back(bad.arguments[0]);refused(bad,policy);}
        for(std::size_t i=0;i<original.arguments.size();++i)for(unsigned poison=0;poison<7;++poison){bad=original;auto &span=bad.arguments[i].value.content;const auto maximum=span.invariant?64:256;
            if(poison==0)span.invariant=!span.invariant;
            if(poison==1)span.text="\x1b";
            if(poison==2)span.text=std::string(maximum+1,'x');
            if(poison==3)span.ascii=std::string(maximum+1,'x');
            if(poison==4){span.text="\xC3";span.ascii="safe";}
            if(poison==5)span.ascii="\x1b";
            if(poison==6){if(span.invariant)span.ascii="substituted";else span.text.clear();}
            refused(bad,policy);}
    }
    auto request=requestFor(i18n::electricityValueLocalePacks()[0],Key::EditInput);TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=1024;TerminalMessageLine line;
    for(const auto &input:std::vector<std::string>{"","0","{input}",std::string(64,'x')}){request.arguments[0].value.content.text=input;CHECK(layoutTerminalElectricityValueMessage(request,policy,&line)&&unisolate(line.line.line.text)=="> "+input+"_");}
    request.arguments[0].value.content.text=u8"١";refused(request,policy);request=requestFor(i18n::electricityValueLocalePacks()[0],Key::FieldValue);
    for(auto &arg:request.arguments)if(arg.name=="value")arg.value.content.text="-"+std::string(38,'9');
    CHECK(layoutTerminalElectricityValueMessage(request,policy,&line)&&!line.line.line.clipped);
    policy.columns=19;CHECK(layoutTerminalElectricityValueMessage(request,policy,&line));std::string clipped;
    for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]=="value")clipped+=run.content.text;
    CHECK(clipped.empty());PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalElectricityValueMessage(request,policy,128,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    policy.columns=1024;request=requestFor(i18n::electricityValueLocalePacks()[0],Key::MissingFieldValue);request.language="en-US";CHECK(layoutTerminalElectricityValueMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    request.key=Key::Count;refused(request,policy);request.key=Key::MissingFieldValue;request.language="en@private";refused(request,policy);
    CHECK(!layoutTerminalElectricityValueMessage({},policy,nullptr)&&!wrapTerminalElectricityValueMessage({},policy,1,nullptr)&&!renderTerminalElectricityValueMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalElectricityValueMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack electricity request/input envelopes with human toggles, clocks and literal missing declarations");}
