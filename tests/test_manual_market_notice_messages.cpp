// Local editor notices only; declared instants and successful owner actions remain distinct from printed text.
#include "product/terminal_settings_message.h"
#include "product/manual_market_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::ManualMarketNoticeMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::manualMarketNoticeMessageKeyCount==9&&i18n::manualMarketNoticeLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,9> english={{"Enter a positive decimal: <=38 digits / <=18 decimals; no exponent or symbol.","Validity: integer 60..86400 seconds; no automatic refresh.","Maximum FX reference-time skew: integer 0..3600 seconds.","Enter UTC Unix seconds, not milliseconds/local time; [Esc] cancels.","Saved; --no-market still suppresses this session's display.","Saved. Manual reference display only; no miner restart or network request.","Save failed; draft retained and active display unchanged.","UTC clock unavailable; reference time unchanged.","Reference stamped NOW by explicit action; use only for a reference from now."}};
    for(std::size_t key=0;key<9;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::manualMarketNoticeMessageKeyName(k))).second&&i18n::manualMarketNoticeMessageKeyName(k).find("manual_market.notice.")==0&&i18n::manualMarketNoticeEnglish(k)==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::manualMarketNoticeLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<9;++key){const auto message=i18n::manualMarketNoticeMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            CHECK(i18n::templateParameters(message.text,&parameters)&&parameters.empty());
            for(const auto *token:{"<=38","<=18","60..86400","0..3600","FX","UTC","Unix","[Esc]","--no-market","NOW"})if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::manualMarketNoticeMessageKeyName(Key::Count).empty()&&i18n::manualMarketNoticeMessage("en",Key::Count).text.empty()&&i18n::manualMarketNoticeEnglish(Key::Count).empty());
}
void native(){
    constexpr uint64_t now=1800000000;ManualMarketPreferences original;original.enabled=original.fxEnabled=true;original.prlUsdt="2.5";original.usdtUsd="0.8";original.prlAsOfUtc=now-10;original.fxAsOfUtc=now-20;original.confirmedUtc=now;
    ManualMarketSettingsPanel panel;
    const auto reject=[&](unsigned field,const std::string &input,Key notice){panel.open(original,{});panel.field=field;panel.key('\r',{},now);panel.input=input;panel.key('\r',{},now);
        CHECK(panel.message==i18n::manualMarketNoticeEnglish(notice)&&panel.page==ManualMarketSettingsPage::Edit&&panel.active==original&&panel.draft==original);};
    reject(1,"1e3",Key::InvalidDecimal);reject(4,"0",Key::InvalidDecimal);reject(6,"59",Key::InvalidValidity);reject(7,"3601",Key::InvalidSkew);reject(2,"1800000000000",Key::InvalidReference);reject(5,"+1",Key::InvalidReference);
    unsigned saves=0;const auto save=[&](ManualMarketPreferences &,std::string *){++saves;return true;};
    for(bool suppressed:{false,true}){panel.open(original,{true,suppressed,original});panel.draft.prlUsdt="3";panel.key('v',{},now);panel.key('\r',save,now+100);
        CHECK(panel.message==i18n::manualMarketNoticeEnglish(suppressed?Key::SavedCliDisabled:Key::Saved)&&!panel.dirty()&&panel.active.prlUsdt=="3");
        CHECK(panel.active.confirmedUtc==now+100&&panel.active.prlAsOfUtc==now-10&&panel.active.fxAsOfUtc==now-20);
        CHECK(panel.context.effective==(suppressed?original:panel.active));}
    CHECK(saves==2);
    panel.open(original,{});panel.draft.prlUsdt="3";panel.key('v',{},now);panel.key('\r',[](ManualMarketPreferences &,std::string *){return false;},now+100);
    CHECK(panel.message==i18n::manualMarketNoticeEnglish(Key::SaveFailed)&&panel.active==original&&panel.draft.prlUsdt=="3"&&panel.draft.confirmedUtc==now&&panel.page==ManualMarketSettingsPage::Review);
    panel.key('\r',[](ManualMarketPreferences &,std::string *error){*error="fixture untouched error";return false;},now+100);CHECK(panel.message=="fixture untouched error"&&panel.active==original);
    for(unsigned field:{2u,5u}){panel.open(original,{});panel.field=field;
        for(uint64_t unavailable:{uint64_t{0},kManualLastUtc+1}){panel.key('n',{},unavailable);CHECK(panel.message==i18n::manualMarketNoticeEnglish(Key::ClockUnavailable)&&panel.draft==original);}
        panel.key('N',{},now+1);CHECK(panel.message==i18n::manualMarketNoticeEnglish(Key::StampedNow)&&panel.active==original&&panel.draft.confirmedUtc==now);
        CHECK(panel.draft.prlAsOfUtc==(field==2?now+1:now-10)&&panel.draft.fxAsOfUtc==(field==5?now+1:now-20));}
    CHECK(panel.key(3,{},now)==ManualMarketSettingsResult::Quit);
}
void matrix(){
    for(const auto &pack:i18n::manualMarketNoticeLocalePacks())for(std::size_t key=0;key<9;++key){TerminalManualMarketNoticeMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<Key>(key);request.literalTone=Tone::Green;
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalManualMarketNoticeMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size()&&line.metadata.sources[run.content.sourceSpan].empty()&&run.tone==Tone::Green);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalManualMarketNoticeMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover))CHECK(unisolate(joined)==i18n::manualMarketNoticeMessage(recover?"en":pack.id,request.key).text&&!line.line.line.clipped);
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalManualMarketNoticeMessage(request,policy,128,&rows)&&renderWrappedTerminalManualMarketNoticeMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<10;++mode){TerminalManualMarketNoticeMessageRequest bad;TerminalTextPolicy policy;policy.order=order;
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
        CHECK(!layoutTerminalManualMarketNoticeMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalManualMarketNoticeMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalManualMarketNoticeMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalManualMarketNoticeMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    TerminalManualMarketNoticeMessageRequest request;request.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalManualMarketNoticeMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    policy.columns=3;request.key=Key::InvalidDecimal;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalManualMarketNoticeMessage(request,policy,1,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalManualMarketNoticeMessage({},policy,nullptr)&&!wrapTerminalManualMarketNoticeMessage({},policy,1,nullptr)&&!renderTerminalManualMarketNoticeMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalManualMarketNoticeMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack manual-reference local notices without quote refresh or inferred reference times");}
