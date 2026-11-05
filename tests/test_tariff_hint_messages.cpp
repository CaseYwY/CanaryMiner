// Editor guidance only: field-name selection and typed drafts neither validate nor activate a tariff declaration.
#include "product/terminal_settings_message.h"
#include "product/tariff_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::TariffHintMessageKey;
const std::array<std::string,11> english={{"none / progressive / all_units; additive shared-register bands.","constant / calendar / dynamic. Clear incompatible tables explicitly.","utc / fixed_standard (no DST or OS timezone inference).","YYYY-MM-DD; clear for null (no exact-date condition).","Canonical decimal; clear for null. Final band MUST be unbounded/null.","UTC Unix MILLISECONDS. Cycle/revision end is exclusive; no automatic now/reset.","Explicit whole-meter opening; subtotal BEFORE multiplier. Missing history is not zero.","Bit mask: weekdays Monday bit 0 (1..127); months January bit 0 (1..4095).","Whole-minute STANDARD offset in seconds, +/-64800. UTC requires 0.","Local minute 0..1440, begin < end. Split overnight windows.","Explicit local declaration. Decimal strings: no exponent; zero must be entered. No account/credentials."}};
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::tariffHintMessageKeyCount==11&&i18n::tariffHintLocalePacks().size()==16);std::set<std::string> identities;
    for(std::size_t key=0;key<11;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::tariffHintMessageKeyName(k))).second&&i18n::tariffHintMessageKeyName(k).find("tariff.hint.")==0&&i18n::tariffHintEnglish(k)==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::tariffHintLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<11;++key){const auto message=i18n::tariffHintMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            CHECK(i18n::templateParameters(message.text,&parameters)&&parameters.empty());
            for(const auto *token:{"none / progressive / all_units","constant / calendar / dynamic","utc / fixed_standard","DST","OS","YYYY-MM-DD","null","MUST","UTC","Unix","MILLISECONDS","BEFORE","1..127","1..4095","STANDARD","+/-64800","0..1440","begin < end"})if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::tariffHintMessageKeyName(Key::Count).empty()&&i18n::tariffHintMessage("en",Key::Count).text.empty()&&i18n::tariffHintEnglish(Key::Count).empty());
}
void native(){
    const std::vector<std::pair<std::string,Key>> cases={{"tiers",Key::Tiers},{"time_mode",Key::TimeMode},{"kind",Key::ClockKind},{"date",Key::Date},
        {"minimum_cycle_subtotal",Key::OptionalDecimal},{"through_kwh",Key::OptionalDecimal},{"through_utc_ms",Key::UtcMs},{"begin_utc_ms",Key::UtcMs},{"end_utc_ms",Key::UtcMs},
        {"imported_kwh",Key::Opening},{"accrued_subtotal",Key::Opening},{"weekdays_mask",Key::Mask},{"months_mask",Key::Mask},{"fixed_offset_seconds",Key::FixedOffset},
        {"begin_minute",Key::Minute},{"end_minute",Key::Minute},{"country",Key::Default},{"source_reference",Key::Default},{"rate_per_kwh",Key::Default},
        {"utc_ms_mask_minute",Key::UtcMs},{"mask_minute",Key::Mask},{"fixed_offset_seconds_minute",Key::Minute},{"date_mask",Key::Mask},{"tiers_minute",Key::Minute},
        {"UTC_MS",Key::Default},{"DATE",Key::Default},{"time_modes",Key::Default},{"kinds",Key::Default},{"",Key::Default}};
    for(const auto &entry:cases){JsonValue seed,value;seed.type=JsonValue::Type::Object;value.type=JsonValue::Type::String;value.text="synthetic-{input}";seed.members.emplace_back(entry.first,value);
        TariffSettingsPanel panel;panel.open(seed);const auto before=tariffJson(seed);CHECK(panel.key('\r',{},1800000000)==TariffSettingsResult::None&&panel.page==TariffSettingsPage::Edit);
        const auto text=panel.render(1024,1000,ColorLevel::None);CHECK(text.find("\n"+english[static_cast<std::size_t>(entry.second)]+"\n")!=text.npos);
        for(std::size_t key=0;key<11;++key)if(key!=static_cast<std::size_t>(entry.second))CHECK(text.find("\n"+english[key]+"\n")==text.npos);
        CHECK(tariffJson(panel.active)==before&&tariffJson(panel.draft)==before&&!panel.dirty()&&panel.message.empty()&&panel.input==value.text);
        CHECK(panel.key(27,{},1800000000)==TariffSettingsResult::None&&panel.page==TariffSettingsPage::Browse&&!panel.dirty());
        CHECK(panel.key(3,{},1800000000)==TariffSettingsResult::Quit);
    }
}
void matrix(){
    for(const auto &pack:i18n::tariffHintLocalePacks())for(std::size_t key=0;key<11;++key){TerminalTariffHintMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<Key>(key);request.literalTone=Tone::Green;
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalTariffHintMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size()&&line.metadata.sources[run.content.sourceSpan].empty()&&run.tone==Tone::Green);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalTariffHintMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover))CHECK(unisolate(joined)==i18n::tariffHintMessage(recover?"en":pack.id,request.key).text&&!line.line.line.clipped);
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalTariffHintMessage(request,policy,128,&rows)&&renderWrappedTerminalTariffHintMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<10;++mode){TerminalTariffHintMessageRequest bad;TerminalTextPolicy policy;policy.order=order;
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
        CHECK(!layoutTerminalTariffHintMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalTariffHintMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalTariffHintMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalTariffHintMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    TerminalTariffHintMessageRequest request;request.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalTariffHintMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    policy.columns=3;request.key=Key::Opening;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalTariffHintMessage(request,policy,1,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalTariffHintMessage({},policy,nullptr)&&!wrapTerminalTariffHintMessage({},policy,1,nullptr)&&!renderTerminalTariffHintMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalTariffHintMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack offline tariff editor hints without numeric, clock, opening or source validation authority");}
