// Literal electricity shell only; description does not certify bills, readings, exclusions or effective activation.
#include "product/terminal_settings_message.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::ElectricityPanelMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
std::vector<std::string> keys(std::string_view text){std::vector<std::string> result;std::size_t at=0;
    while((at=text.find('[',at))!=text.npos){const auto end=text.find(']',at);CHECK(end!=text.npos);if(end==text.npos)break;result.emplace_back(text.substr(at,end-at+1));at=end+1;}return result;}
void catalog(){
    CHECK(i18n::electricityPanelMessageKeyCount==25&&i18n::electricityPanelLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,25> english={{"Terminal too short.","Resize to 12+ rows.","[Ctrl+C] Stop [Esc] Back","[Enter] Apply draft [Esc] Cancel edit [Ctrl+C] Stop","[Enter/F2] Save next-session defaults [Arrows] Scroll [Esc] Edit [Q] Stop","[Tab/Arrows] Field [Enter/Space] Change [N] Start UTC now [V/F2] Review [Esc] Back [Q] Stop","GPU BOARD / SESSION ONLY. Flat ENERGY-ONLY is not a utility bill or net income.","Electricity/energy"," *","Signed decimal currency/kWh; zero valid. No exponent or implicit FX.","Printable ASCII, max 64. Do NOT enter account, wallet or credentials.","Whole-minute fixed STANDARD seconds (+/-64800), not an IANA/DST zone.","UTC Unix seconds, not milliseconds or local time.","Milliseconds; selection >=500; max gap >= selection and <=60000.","Q/D/L/S are text here, not global actions.","Review electricity-preferences.json ONLY","No value changes.","Activation: NEXT EXPLICIT parent session only, never on child retry.","Current --no-energy overrides collection, including after Save.","UI/runtime/manual-market drafts save separately. No history deletion.","Flat energy-only EXCLUDES tiers/TOU/dynamic/fees/tax/shared-meter bill effects.","No hardware changes, income/net forecast, optimizer, network or mining permission.","Unsaved electricity settings","[Enter] Review/save [D] Discard [Esc] Continue editing","No country, supplier, tariff, currency or OS timezone inference."}};
    for(std::size_t key=0;key<25;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::electricityPanelMessageKeyName(k))).second&&i18n::electricityPanelMessageKeyName(k).find("electricity.panel.")==0&&i18n::electricityPanelEnglish(k)==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::electricityPanelLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<25;++key){const auto message=i18n::electricityPanelMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            CHECK(i18n::templateParameters(message.text,&parameters)&&parameters.empty()&&keys(english[key])==keys(message.text));
            for(const auto *token:{"12","GPU BOARD","ENERGY-ONLY","kWh","FX","ASCII","64","STANDARD","+/-64800","IANA/DST","UTC","Unix",">=500",">=","<=60000","Q/D/L/S","Ctrl+C","electricity-preferences.json","NEXT EXPLICIT","--no-energy","UI","TOU","OS"})
                if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::electricityPanelMessageKeyName(Key::Count).empty()&&i18n::electricityPanelMessage("en",Key::Count).text.empty()&&i18n::electricityPanelEnglish(Key::Count).empty());
}
void matrix(){
    for(const auto &pack:i18n::electricityPanelLocalePacks())for(std::size_t key=0;key<25;++key){TerminalElectricityPanelMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<Key>(key);request.literalTone=Tone::Green;
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalElectricityPanelMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size()&&line.metadata.sources[run.content.sourceSpan].empty()&&run.tone==Tone::Green);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalElectricityPanelMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover))CHECK(unisolate(joined)==i18n::electricityPanelMessage(recover?"en":pack.id,request.key).text&&!line.line.line.clipped);
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalElectricityPanelMessage(request,policy,128,&rows)&&renderWrappedTerminalElectricityPanelMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<10;++mode){TerminalElectricityPanelMessageRequest bad;TerminalTextPolicy policy;policy.order=order;
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
        CHECK(!layoutTerminalElectricityPanelMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalElectricityPanelMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalElectricityPanelMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalElectricityPanelMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    TerminalElectricityPanelMessageRequest request;request.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalElectricityPanelMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    policy.columns=3;request.key=Key::Exclusions;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalElectricityPanelMessage(request,policy,1,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalElectricityPanelMessage({},policy,nullptr)&&!wrapTerminalElectricityPanelMessage({},policy,1,nullptr)&&!renderTerminalElectricityPanelMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalElectricityPanelMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();matrix();failures();return finishTests("sixteen-pack literal electricity shell and limitations without sensor, bill or runtime authority");}
