// Literal manual-reference shell only; descriptions supply no quote, reference-time, source or runtime authority.
#include "product/terminal_settings_message.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::ManualMarketPanelMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
std::vector<std::string> keys(std::string_view text){std::vector<std::string> result;std::size_t at=0;
    while((at=text.find('[',at))!=text.npos){const auto end=text.find(']',at);CHECK(end!=text.npos);if(end==text.npos)break;result.emplace_back(text.substr(at,end-at+1));at=end+1;}return result;}
void catalog(){
    CHECK(i18n::manualMarketPanelMessageKeyCount==28&&i18n::manualMarketPanelLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,28> english={{"Terminal too short.","Resize to 12+ rows.","[Ctrl+C] Stop [Esc] Back","[Enter] Apply draft [Esc] Cancel edit [Ctrl+C] Stop","[Enter/F2] Save [Arrows] Scroll [Esc] Edit [Q] Stop","[Tab/Arrows] Field [Enter/Space] Change [N] Stamp UTC [V/F2] Review [Esc] Back [Q] Stop","Manual input only; stored confirmation is NOT a quote timestamp.","Manual/offline market"," *","Positive base-10 decimal; empty clears this pair.","Validity in seconds (60..86400), not refresh cadence.","Maximum separation of PRL/FX reference times (0..3600 seconds).","Reference UTC Unix seconds. [N] from browse explicitly stamps now.","Q/D/L/S are text here. Ctrl+C still stops.","Unsaved offline-market settings","[Enter] Review/save [D] Discard [Esc] Continue editing","Review manual-market-preferences.json only","No value changes; saving does not refresh reference times.","Activation: live terminal references only; no restart.","Activation: next explicit dashboard session; no mining was started.","Current CLI --no-market wins over saved preferences.","No HTTPS, trading, actual income, forecasts or economic control.","Prices/FX stay local; UI/runtime drafts and donation are untouched.","Identity: Pearl mainnet native PRL; quote USDT; FX USD per USDT.","Current display: OFF (--no-market CLI override)","No active mining session; local settings only.","Fields above are configured values; current inputs change only after successful Save.","No provider fetch or 1h average. Manual references are not income."}};
    for(std::size_t key=0;key<28;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::manualMarketPanelMessageKeyName(k))).second&&i18n::manualMarketPanelMessageKeyName(k).find("manual_market.panel.")==0&&i18n::manualMarketPanelEnglish(k)==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::manualMarketPanelLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<28;++key){const auto message=i18n::manualMarketPanelMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            CHECK(i18n::templateParameters(message.text,&parameters)&&parameters.empty()&&keys(english[key])==keys(message.text));
            for(const auto *token:{"12","60..86400","0..3600","PRL","FX","USDT","USD","Pearl","UTC","Unix","Q/D/L/S","Ctrl+C","manual-market-preferences.json","--no-market","CLI","UI","HTTPS","1h","OFF"})
                if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::manualMarketPanelMessageKeyName(Key::Count).empty()&&i18n::manualMarketPanelMessage("en",Key::Count).text.empty()&&i18n::manualMarketPanelEnglish(Key::Count).empty());
}
void matrix(){
    for(const auto &pack:i18n::manualMarketPanelLocalePacks())for(std::size_t key=0;key<28;++key){TerminalManualMarketPanelMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<Key>(key);request.literalTone=Tone::Green;
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalManualMarketPanelMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size()&&line.metadata.sources[run.content.sourceSpan].empty()&&run.tone==Tone::Green);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalManualMarketPanelMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover))CHECK(unisolate(joined)==i18n::manualMarketPanelMessage(recover?"en":pack.id,request.key).text&&!line.line.line.clipped);
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalManualMarketPanelMessage(request,policy,128,&rows)&&renderWrappedTerminalManualMarketPanelMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<10;++mode){TerminalManualMarketPanelMessageRequest bad;TerminalTextPolicy policy;policy.order=order;
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
        CHECK(!layoutTerminalManualMarketPanelMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalManualMarketPanelMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalManualMarketPanelMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalManualMarketPanelMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    TerminalManualMarketPanelMessageRequest request;request.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalManualMarketPanelMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    policy.columns=3;request.key=Key::NoFinance;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalManualMarketPanelMessage(request,policy,1,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalManualMarketPanelMessage({},policy,nullptr)&&!wrapTerminalManualMarketPanelMessage({},policy,1,nullptr)&&!renderTerminalManualMarketPanelMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalManualMarketPanelMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();matrix();failures();return finishTests("sixteen-pack literal manual-reference shell without quote, timestamp or financial authority");}
