// Frozen declaration display only; presence-specific blank forms preserve developer rendering, not valid tariffs.
#include "product/terminal_settings_message.h"
#include "product/electricity_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::ElectricityTariffMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::electricityTariffMessageKeyCount==9&&i18n::electricityTariffLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,9> english={{"Current tariff:  /kWh | ","Current tariff:  /kWh | {version}","Current tariff:  {currency}/kWh | ","Current tariff:  {currency}/kWh | {version}","Current tariff: {rate} /kWh | ","Current tariff: {rate} /kWh | {version}","Current tariff: {rate} {currency}/kWh | ","Current tariff: {rate} {currency}/kWh | {version}","Current tariff: off/no active collection"}};
    for(std::size_t key=0;key<9;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::electricityTariffMessageKeyName(k))).second&&i18n::electricityTariffMessageKeyName(k).find("electricity.tariff.")==0&&i18n::electricityTariffMessage("en",k).text==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::electricityTariffLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<9;++key){const auto message=i18n::electricityTariffMessage(pack.id,static_cast<Key>(key));CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));if(key<8)CHECK(message.text.find("/kWh")!=message.text.npos);}}
    for(bool rate:{false,true})for(bool currency:{false,true})for(bool version:{false,true}){i18n::MessageArguments args;if(rate)args.emplace_back("rate","0");if(currency)args.emplace_back("currency","USD");if(version)args.emplace_back("version","synthetic-v1");
        const auto expected=std::string("Current tariff: ")+(rate?"0":"")+" "+(currency?"USD":"")+"/kWh | "+(version?"synthetic-v1":"");
        const auto key=i18n::electricityTariffPresenceKey(rate,currency,version);CHECK(static_cast<unsigned>(key)==(rate?4u:0u)+(currency?2u:0u)+(version?1u:0u)&&i18n::electricityTariffEnglish(key,args)==expected);}
    CHECK(i18n::electricityTariffEnglish(Key::CurrentAll,{{"version","{rate}"},{"currency","{version}"},{"rate","{currency}"}})=="Current tariff: {currency} {version}/kWh | {rate}");
    CHECK(i18n::electricityTariffEnglish(Key::Off)==english[8]);
    for(const auto &input:std::vector<std::string>{"",u8"ع","\x1b",std::string(65,'x')})CHECK(i18n::electricityTariffEnglish(Key::CurrentRate,{{"rate",input}}).empty());
    CHECK(!i18n::electricityTariffEnglish(Key::CurrentRate,{{"rate",std::string(64,'x')}}).empty());
    CHECK(i18n::electricityTariffEnglish(Key::CurrentAll,{{"rate","x"}}).empty()&&i18n::electricityTariffEnglish(Key::CurrentCurrencyVersion,{{"currency","x"},{"currency","y"}}).empty()&&i18n::electricityTariffEnglish(Key::Off,{{"rate","unused"}}).empty());
    CHECK(i18n::electricityTariffMessageKeyName(Key::Count).empty()&&i18n::electricityTariffMessage("en",Key::Count).text.empty()&&i18n::electricityTariffEnglish(Key::Count).empty());
}
void native(){
    for(bool rate:{false,true})for(bool currency:{false,true})for(bool version:{false,true})for(bool running:{false,true})for(bool collection:{false,true})for(bool enabled:{false,true})for(bool cli:{false,true}){
        ElectricityPreferences effective;effective.collectEnergy=collection;effective.tariffEnabled=enabled;effective.ratePerKwh=rate?"0":"";effective.currency=currency?"USD":"";effective.version=version?"{rate}":"";
        ElectricitySettingsPanel panel;panel.open({}, {running,cli,effective});const auto original=electricityPreferencesJson(panel.draft),current=electricityPreferencesJson(effective);
        // Deliberately includes incomplete/inconsistent developer contexts; never preference, tariff or source admission.
        const auto expected=std::string("Current tariff: ")+(running&&collection&&enabled&&!cli?effective.ratePerKwh+" "+effective.currency+"/kWh | "+effective.version:"off/no active collection");
        for(auto page:{ElectricitySettingsPage::Browse,ElectricitySettingsPage::Review}){panel.page=page;CHECK(panel.render(1024,1000,ColorLevel::None).find(expected+"\n")!=std::string::npos);}
        CHECK(electricityPreferencesJson(panel.draft)==original&&electricityPreferencesJson(panel.context.effective)==current);
    }
}
TerminalElectricityTariffMessageRequest requestFor(const i18n::ElectricityTariffLocalePack &pack,Key key){
    TerminalElectricityTariffMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;std::vector<std::string_view> names;
    CHECK(i18n::templateParameters(i18n::electricityTariffMessage(pack.id,key).text,&names));
    for(const auto name:names)request.arguments.insert(request.arguments.begin(),{std::string(name),{{name=="rate"?"-2.5":name=="currency"?"USD":"synthetic-v1",{},true},Tone::Red}});
    return request;
}
void matrix(){
    for(const auto &pack:i18n::electricityTariffLocalePacks())for(std::size_t key=0;key<9;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalElectricityTariffMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:Tone::Red));}}
            for(const auto &arg:request.arguments){std::string actual;for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]==arg.name)actual+=run.content.text;CHECK(actual.empty()||actual==arg.value.content.text);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalElectricityTariffMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){const auto text=i18n::electricityTariffMessage(recover?"en":pack.id,request.key).text;std::string expected;std::size_t at=0;
                while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){expected+=text.substr(at);break;}expected+=text.substr(at,start-at);const auto end=text.find('}',start);const auto name=text.substr(start+1,end-start-1);
                    const auto arg=std::find_if(request.arguments.begin(),request.arguments.end(),[&](const auto &a){return a.name==name;});CHECK(arg!=request.arguments.end());expected+=arg->value.content.text;at=end+1;}
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=29;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalElectricityTariffMessage(request,policy,128,&rows)&&renderWrappedTerminalElectricityTariffMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=29);}
    }
}
void refused(const TerminalElectricityTariffMessageRequest &bad,const TerminalTextPolicy &policy){
    TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
    PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
    CHECK(!layoutTerminalElectricityTariffMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
    CHECK(!wrapTerminalElectricityTariffMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
    CHECK(!renderTerminalElectricityTariffMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
    CHECK(!renderWrappedTerminalElectricityTariffMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});
}
void failures(){
    for(const auto &pack:i18n::electricityTariffLocalePacks())for(std::size_t key=0;key<9;++key)for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
        const auto original=requestFor(pack,static_cast<Key>(key));TerminalTextPolicy policy;policy.order=order;auto bad=original;
        bad.arguments.push_back({"extra",{{"x",{},true},Tone::Red}});refused(bad,policy);
        if(!original.arguments.empty()){bad=original;bad.arguments.pop_back();refused(bad,policy);bad=original;bad.arguments.push_back(bad.arguments[0]);refused(bad,policy);}
        for(std::size_t i=0;i<original.arguments.size();++i)for(unsigned poison=0;poison<8;++poison){bad=original;auto &span=bad.arguments[i].value.content;
            if(poison==0)span.invariant=false;
            if(poison==1)span.text.clear();
            if(poison==2)span.text="\x1b";
            if(poison==3)span.text=std::string(65,'x');
            if(poison==4)span.ascii=std::string(65,'x');
            if(poison==5){span.text="\xC3";span.ascii="safe";}
            if(poison==6)span.ascii="\x1b";
            if(poison==7)span.ascii="substituted";
            refused(bad,policy);}
    }
    auto request=requestFor(i18n::electricityTariffLocalePacks()[0],Key::CurrentRate);TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=1024;TerminalMessageLine line;
    for(const auto &text:std::vector<std::string>{"0","-2.5","{rate}",std::string(64,'x')}){request.arguments[0].value.content.text=text;CHECK(layoutTerminalElectricityTariffMessage(request,policy,&line)&&unisolate(line.line.line.text)=="Current tariff: "+text+" /kWh | ");}
    request.arguments[0].value.content.text=u8"١";refused(request,policy);request.arguments[0].value.content.text=std::string(64,'x');policy.columns=19;
    PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalElectricityTariffMessage(request,policy,128,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    policy.columns=1024;request=requestFor(i18n::electricityTariffLocalePacks()[0],Key::Off);request.language="en-US";CHECK(layoutTerminalElectricityTariffMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    request.key=Key::Count;refused(request,policy);request.key=Key::Off;request.language="en@private";refused(request,policy);request.language="en";request.literalTone=static_cast<Tone>(99);refused(request,policy);
    CHECK(!layoutTerminalElectricityTariffMessage({},policy,nullptr)&&!wrapTerminalElectricityTariffMessage({},policy,1,nullptr)&&!renderTerminalElectricityTariffMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalElectricityTariffMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack complete frozen-current-tariff rows without inferred rate, currency, version or source facts");}
