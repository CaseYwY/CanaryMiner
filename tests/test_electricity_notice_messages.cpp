// Local input/save descriptions only; no actual sensor, tariff, utility, supplier-time or runtime authority.
#include "product/terminal_settings_message.h"
#include "product/electricity_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::ElectricityNoticeMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::electricityNoticeMessageKeyCount==12&&i18n::electricityNoticeLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,12> english={{"Input exceeds 64 characters; nothing applied. Edit shorter or cancel.","This editor currently requires ASCII; non-ASCII input was not applied. Cancel or edit again.","Saved for NEXT EXPLICIT SESSION ONLY; current collection/tariff unchanged. No miner restart.","Save failed; draft retained and current session unchanged.","Effective start explicitly stamped now; this is a user declaration, not a supplier timestamp.","Input was too long or non-ASCII; nothing applied. Shorten/edit or cancel.","Use <=64 printable ASCII characters, no surrounding spaces.","Currency: three UPPERCASE letters, no symbol or implicit FX.","Enter a signed decimal <=38 digits / <=18 decimals; zero valid, no exponent.","Enter an integer in the stated units; UTC uses seconds, not milliseconds.","Milliseconds must be 500..60000; maximum gap must also cover selection.","Select fixed STANDARD clock first; offset in whole-minute seconds within +/-64800."}};
    for(std::size_t key=0;key<12;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::electricityNoticeMessageKeyName(k))).second&&i18n::electricityNoticeMessageKeyName(k).find("electricity.notice.")==0&&i18n::electricityNoticeEnglish(k)==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::electricityNoticeLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<12;++key){const auto message=i18n::electricityNoticeMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            CHECK(i18n::templateParameters(message.text,&parameters)&&parameters.empty());
            for(const auto *token:{"64","ASCII","NEXT EXPLICIT SESSION ONLY","<=64","UPPERCASE","FX","<=38","<=18","UTC","500..60000","STANDARD","+/-64800"})if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::electricityNoticeMessageKeyName(Key::Count).empty()&&i18n::electricityNoticeMessage("en",Key::Count).text.empty()&&i18n::electricityNoticeEnglish(Key::Count).empty());
}
void native(){
    constexpr uint64_t now=1800000000;ElectricitySettingsPanel panel;panel.open({},{});panel.field=6;panel.key('\r',{},now);
    for(unsigned i=0;i<65;++i)panel.key('x',{},now);
    CHECK(panel.message==i18n::electricityNoticeEnglish(Key::InputTooLong)&&panel.input.size()==64&&panel.inputRejected&&panel.draft.country.empty()&&!panel.dirty());
    panel.key('\r',{},now);CHECK(panel.message==i18n::electricityNoticeEnglish(Key::RejectedInput)&&panel.page==ElectricitySettingsPage::Edit&&panel.draft.country.empty());
    panel.open({},{});panel.field=6;panel.key('\r',{},now);panel.key(200,{},now);
    CHECK(panel.message==i18n::electricityNoticeEnglish(Key::NonAscii)&&panel.inputRejected&&panel.draft.country.empty());
    panel.key('\r',{},now);CHECK(panel.message==i18n::electricityNoticeEnglish(Key::RejectedInput)&&!panel.dirty());
    const auto reject=[&](unsigned field,const std::string &input,Key notice){panel.open({},{});panel.field=field;panel.key('\r',{},now);panel.input=input;panel.key('\r',{},now);
        CHECK(panel.message==i18n::electricityNoticeEnglish(notice)&&panel.page==ElectricitySettingsPage::Edit&&panel.active==ElectricityPreferences{}&&panel.draft==ElectricityPreferences{});};
    reject(6," surrounded ",Key::InvalidLabel);reject(11,"usd",Key::InvalidCurrency);reject(12,"1e3",Key::InvalidRate);reject(15,"1800000000000",Key::InvalidInteger);reject(2,"499",Key::InvalidInterval);reject(3,"60001",Key::InvalidInterval);reject(14,"60",Key::InvalidOffset);
    panel.open({},{});panel.field=12;panel.key('\r',{},now);panel.input="-000.000";panel.key('\r',{},now);CHECK(panel.draft.ratePerKwh=="0"&&panel.page==ElectricitySettingsPage::Browse&&panel.message.empty());
    panel.key('\r',{},now);panel.input="-2.500";panel.key('\r',{},now);CHECK(panel.draft.ratePerKwh=="-2.5"&&panel.active.ratePerKwh.empty());
    ElectricityPreferences frozen;frozen.collectEnergy=true;panel.open(frozen,{true,false,frozen});panel.draft.collectEnergy=false;panel.key('v',{},now);unsigned saves=0;
    panel.key('\r',[&](ElectricityPreferences &,std::string *){++saves;return true;},now);
    CHECK(saves==1&&panel.message==i18n::electricityNoticeEnglish(Key::Saved)&&!panel.dirty()&&!panel.active.collectEnergy&&panel.active.confirmedUtc==now&&panel.context.effective==frozen);
    panel.open({},{});panel.draft.showEnergy=false;panel.key('v',{},now);panel.key('\r',[](ElectricityPreferences &,std::string *){return false;},now);
    CHECK(panel.message==i18n::electricityNoticeEnglish(Key::SaveFailed)&&panel.active.showEnergy&&!panel.draft.showEnergy&&panel.draft.confirmedUtc==0&&panel.page==ElectricitySettingsPage::Review);
    panel.key('\r',[](ElectricityPreferences &,std::string *error){*error="fixture untouched error";return false;},now);CHECK(panel.message=="fixture untouched error"&&panel.active.showEnergy);
    panel.open({},{});panel.field=15;panel.key('N',{},now);CHECK(panel.message==i18n::electricityNoticeEnglish(Key::StampedNow)&&panel.draft.effectiveFromUtc==now&&panel.active.effectiveFromUtc==0&&panel.draft.confirmedUtc==0);
    const auto draft=panel.draft;for(uint64_t unavailable:{uint64_t{0},uint64_t{253402300800}}){panel.key('n',{},unavailable);CHECK(panel.draft==draft&&panel.active.effectiveFromUtc==0);}
    CHECK(panel.key(3,{},now)==ElectricitySettingsResult::Quit);
}
void matrix(){
    for(const auto &pack:i18n::electricityNoticeLocalePacks())for(std::size_t key=0;key<12;++key){TerminalElectricityNoticeMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<Key>(key);request.literalTone=Tone::Green;
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalElectricityNoticeMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size()&&line.metadata.sources[run.content.sourceSpan].empty()&&run.tone==Tone::Green);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalElectricityNoticeMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover))CHECK(unisolate(joined)==i18n::electricityNoticeMessage(recover?"en":pack.id,request.key).text&&!line.line.line.clipped);
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalElectricityNoticeMessage(request,policy,128,&rows)&&renderWrappedTerminalElectricityNoticeMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<10;++mode){TerminalElectricityNoticeMessageRequest bad;TerminalTextPolicy policy;policy.order=order;
        if(mode==0)bad.key=Key::Count;
        if(mode==1)bad.language="en@private";
        if(mode==2)bad.language="";
        if(mode==3)bad.language=std::string(36,'x');
        if(mode==4)bad.literalTone=static_cast<Tone>(99);
        if(mode==5)policy.columns=4097;
        if(mode==6)policy.direction=static_cast<TerminalTextDirection>(99);
        if(mode==7)policy.ambiguousCells=0;
        if(mode==8)policy.order=static_cast<TerminalTextOrder>(99);
        if(mode==9)policy.ambiguousCells=3;
        TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
        PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
        CHECK(!layoutTerminalElectricityNoticeMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalElectricityNoticeMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalElectricityNoticeMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalElectricityNoticeMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    TerminalElectricityNoticeMessageRequest request;request.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalElectricityNoticeMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    policy.columns=3;request.key=Key::InvalidOffset;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalElectricityNoticeMessage(request,policy,1,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalElectricityNoticeMessage({},policy,nullptr)&&!wrapTerminalElectricityNoticeMessage({},policy,1,nullptr)&&!renderTerminalElectricityNoticeMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalElectricityNoticeMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack electricity local input/save notices with frozen current collection and declarations");}
