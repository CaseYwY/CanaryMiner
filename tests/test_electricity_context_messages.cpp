// Complete model/effective/collection rows; printed states supply no sensor, tariff-time or activation evidence.
#include "product/terminal_settings_message.h"
#include "product/electricity_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::ElectricityContextMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::electricityContextMessageKeyCount==11&&i18n::electricityContextLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,11> english={{"Field {position}/17 | configured next-session defaults","Model/basis: flat ENERGY-ONLY / GPU BOARD proxy; clock {clock}, offset {offset} seconds.","Declared effective UTC: {from} to {through} (end exclusive).","Declared effective UTC: {from} to not supplied (end exclusive).","Declared effective UTC: not supplied to {through} (end exclusive).","Declared effective UTC: not supplied to not supplied (end exclusive).","Current session collection: on","Current session collection: off/no active session","Current collection: off (--no-energy)","Current collection: on (frozen session defaults)","Current collection: off/no active session"}};
    for(std::size_t key=0;key<11;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::electricityContextMessageKeyName(k))).second&&i18n::electricityContextMessageKeyName(k).find("electricity.context.")==0&&i18n::electricityContextMessage("en",k).text==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::electricityContextLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<11;++key){const auto message=i18n::electricityContextMessage(pack.id,static_cast<Key>(key));CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            for(const auto *token:{"/17","ENERGY-ONLY","GPU BOARD","UTC","--no-energy"})if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::electricityContextEnglish(Key::ModelBasis,{{"offset","-64800"},{"clock","fixed STANDARD (no DST)"}})=="Model/basis: flat ENERGY-ONLY / GPU BOARD proxy; clock fixed STANDARD (no DST), offset -64800 seconds.");
    CHECK(i18n::electricityContextEnglish(Key::EffectiveBoth,{{"through","2027-01-16T08:00:00Z"},{"from","2027-01-15T08:00:00Z"}})=="Declared effective UTC: 2027-01-15T08:00:00Z to 2027-01-16T08:00:00Z (end exclusive).");
    CHECK(i18n::electricityContextEnglish(Key::EffectiveFrom,{{"from","[invalid UTC]"}})=="Declared effective UTC: [invalid UTC] to not supplied (end exclusive).");
    CHECK(i18n::electricityContextEnglish(Key::EffectiveBoth,{{"from","{through}"},{"through","{from}"}})=="Declared effective UTC: {through} to {from} (end exclusive).");
    for(std::size_t k=5;k<11;++k)CHECK(i18n::electricityContextEnglish(static_cast<Key>(k))==english[k]);
    for(const auto &input:std::vector<std::string>{"",u8"ع","\x1b",std::string(65,'x')})CHECK(i18n::electricityContextEnglish(Key::EffectiveFrom,{{"from",input}}).empty());
    CHECK(!i18n::electricityContextEnglish(Key::EffectiveFrom,{{"from",std::string(64,'x')}}).empty());
    CHECK(i18n::electricityContextEnglish(Key::EffectiveBoth,{{"from","x"}}).empty()&&i18n::electricityContextEnglish(Key::EffectiveBoth,{{"from","x"},{"from","y"}}).empty()&&i18n::electricityContextEnglish(Key::EffectiveUnset,{{"from","unused"}}).empty());
    CHECK(i18n::electricityContextMessageKeyName(Key::Count).empty()&&i18n::electricityContextMessage("en",Key::Count).text.empty()&&i18n::electricityContextEnglish(Key::Count).empty());
}
void native(){
    for(bool from:{false,true})for(bool through:{false,true})for(bool fixed:{false,true})for(bool running:{false,true})for(bool collection:{false,true})for(bool cli:{false,true}){
        ElectricityPreferences p;p.collectEnergy=collection;p.fixedStandardTime=fixed;p.fixedOffsetSeconds=fixed?-64800:0;p.effectiveFromUtc=from?1800000000:0;p.effectiveThroughUtc=through?1800001000:0;
        ElectricitySettingsPanel panel;panel.open(p,{running,cli,p});const auto original=electricityPreferencesJson(p);panel.page=ElectricitySettingsPage::Review;
        const auto start=from?utcIsoClock(static_cast<time_t>(p.effectiveFromUtc)):"not supplied",end=through?utcIsoClock(static_cast<time_t>(p.effectiveThroughUtc)):"not supplied";
        const auto review=panel.render(1024,1000,ColorLevel::None); // explicit inconsistent developer render states, not preference/source admission
        CHECK(review.find("Declared effective UTC: "+start+" to "+end+" (end exclusive).")!=review.npos);
        CHECK(review.find(std::string("Model/basis: flat ENERGY-ONLY / GPU BOARD proxy; clock ")+(fixed?"fixed STANDARD (no DST)":"explicit UTC")+", offset "+std::to_string(p.fixedOffsetSeconds)+" seconds.")!=review.npos);
        CHECK(review.find(std::string("Current session collection: ")+(running&&collection&&!cli?"on":"off/no active session"))!=review.npos);
        panel.page=ElectricitySettingsPage::Browse;
        for(unsigned field:{0u,8u,16u}){panel.field=field;const auto browse=panel.render(1024,1000,ColorLevel::None);
            CHECK(browse.find("Field "+std::to_string(field+1)+"/17 | configured next-session defaults")!=browse.npos);
            CHECK(browse.find(std::string("Current collection: ")+(cli?"off (--no-energy)":running&&collection?"on (frozen session defaults)":"off/no active session"))!=browse.npos);}
        CHECK(electricityPreferencesJson(panel.active)==original&&electricityPreferencesJson(panel.draft)==original&&electricityPreferencesJson(panel.context.effective)==original);
    }
}
TerminalElectricityContextMessageRequest requestFor(const i18n::ElectricityContextLocalePack &pack,Key key){
    TerminalElectricityContextMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;std::vector<std::string_view> names;
    CHECK(i18n::templateParameters(i18n::electricityContextMessage(pack.id,key).text,&names));
    for(const auto name:names){TerminalSegment value{{name=="offset"?"-64800":name=="from"?"2027-01-15T08:00:00Z":"2027-01-16T08:00:00Z",{},true},Tone::Red};
        if(name=="clock")value={{std::string(i18n::electricityValueMessage(pack.id,i18n::ElectricityValueMessageKey::FixedClock).text),"fixed STANDARD (no DST)"},Tone::Canary};
        if(name=="position")value={{"17","17",false,TerminalTextAtom::LeftToRight},Tone::Canary};
        request.arguments.insert(request.arguments.begin(),{std::string(name),std::move(value)});}
    return request;
}
void matrix(){
    for(const auto &pack:i18n::electricityContextLocalePacks())for(std::size_t key=0;key<11;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalElectricityContextMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:name=="clock"||name=="position"?Tone::Canary:Tone::Red));}}
            for(const auto &arg:request.arguments)if(arg.value.content.invariant||arg.value.content.atom==TerminalTextAtom::LeftToRight){std::string actual;for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]==arg.name)actual+=run.content.text;CHECK(actual.empty()||actual==arg.value.content.text);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalElectricityContextMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){const auto text=i18n::electricityContextMessage(recover?"en":pack.id,request.key).text;std::string expected;std::size_t at=0;
                while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){expected+=text.substr(at);break;}expected+=text.substr(at,start-at);const auto end=text.find('}',start);const auto name=text.substr(start+1,end-start-1);
                    const auto arg=std::find_if(request.arguments.begin(),request.arguments.end(),[&](const auto &a){return a.name==name;});CHECK(arg!=request.arguments.end());const auto &span=arg->value.content;expected+=recover?(span.ascii.empty()?span.text:span.ascii):span.text;at=end+1;}
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=29;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalElectricityContextMessage(request,policy,128,&rows)&&renderWrappedTerminalElectricityContextMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=29);}
    }
}
void refused(const TerminalElectricityContextMessageRequest &bad,const TerminalTextPolicy &policy){
    TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
    PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
    CHECK(!layoutTerminalElectricityContextMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
    CHECK(!wrapTerminalElectricityContextMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
    CHECK(!renderTerminalElectricityContextMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
    CHECK(!renderWrappedTerminalElectricityContextMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});
}
void failures(){
    for(const auto &pack:i18n::electricityContextLocalePacks())for(std::size_t key=0;key<11;++key)for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
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
    auto request=requestFor(i18n::electricityContextLocalePacks()[0],Key::FieldPosition);TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=1024;TerminalMessageLine line;
    request.arguments[0].value.content.atom=TerminalTextAtom::None;refused(request,policy);request.arguments[0].value.content.atom=TerminalTextAtom::LeftToRight;
    request.arguments[0].value.content.text=u8"١٧";CHECK(layoutTerminalElectricityContextMessage(request,policy,&line));
    CHECK(unisolate(line.line.line.text)==(terminalTextBackend().unicode?u8"Field ١٧/17 | configured next-session defaults":"Field 17/17 | configured next-session defaults"));
    request=requestFor(i18n::electricityContextLocalePacks()[0],Key::EffectiveFrom);
    for(const auto &text:std::vector<std::string>{"[invalid UTC]","{from}",std::string(64,'x')}){request.arguments[0].value.content.text=text;CHECK(layoutTerminalElectricityContextMessage(request,policy,&line)&&unisolate(line.line.line.text)=="Declared effective UTC: "+text+" to not supplied (end exclusive).");}
    request.arguments[0].value.content.text=u8"١";refused(request,policy);request.arguments[0].value.content.text=std::string(64,'x');policy.columns=19;
    PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalElectricityContextMessage(request,policy,128,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    policy.columns=1024;request=requestFor(i18n::electricityContextLocalePacks()[0],Key::EffectiveUnset);request.language="en-US";CHECK(layoutTerminalElectricityContextMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    request.key=Key::Count;refused(request,policy);request.key=Key::EffectiveUnset;request.language="en@private";refused(request,policy);request.language="en";request.literalTone=static_cast<Tone>(99);refused(request,policy);
    CHECK(!layoutTerminalElectricityContextMessage({},policy,nullptr)&&!wrapTerminalElectricityContextMessage({},policy,1,nullptr)&&!renderTerminalElectricityContextMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalElectricityContextMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack complete electricity model, declared-time and frozen-collection display envelopes");}
