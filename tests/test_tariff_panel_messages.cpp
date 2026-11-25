// Literal offline shell/review limitations only; no real declaration, supplier, reset, billing or activation evidence.
#include "product/terminal_settings_message.h"
#include "product/tariff_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::TariffPanelMessageKey;
const std::array<std::string,27> english={{"Terminal too short.","Resize to 12+ rows.","[Ctrl+C] Stop [Esc] Back","[Enter] Apply draft [Esc] Cancel edit [Ctrl+C] Stop","[Enter/F2] Save declaration [Arrows] Scroll [Esc] Edit [Q] Stop","[Arrows/Tab] Field [Enter] Open/edit [A] Add row [X] Remove row [V/F2] Review [Esc] Back [Q] Stop","OFFLINE DECLARATION / whole-meter scenario; no automatic reset or runtime activation.","Q/D/L/S are text here; Ctrl+C stops.","Unsaved tariff declaration","[Enter] Review [D] Discard [Esc] Continue editing","[Enter] Remove from draft [Esc] Cancel","Saved declaration is preserved until reviewed Save.","Empty table: [A] Add a blank row","Combined tariff / declared opening"," *","[Enter] fields"," [fixed]","Review tariff-declaration.json ONLY","Saved declaration does NOT activate the runtime energy owner or an optimizer.","Whole-meter DECLARED FORECAST opening; source reference is an unverified local claim.","Reset: explicit single cycle only, <=370 elapsed days. Rates do not reset shared tiers.","EXCLUDES demand/export/prepaid/independent resets/proration/ordered taxes/unsupported rounding.","No missing household usage, future prices or unobserved load is filled with zero.","Minimum/all-units need full remaining cycle. USD conversion is not inferred.","Changes (old -> draft); unchanged fields follow:","No value changes.","Save stamps local confirmation time only; not supplier approval or freshness renewal."}};
const std::array<std::string_view,16> roundingExclusions={{
    u8"unsupported rounding",
    u8"\u4e0d\u652f\u6301\u7684\u820d\u5165\u89c4\u5219",
    u8"\u4e0d\u652f\u63f4\u7684\u6368\u5165\u898f\u5247",
    u8"reglas de redondeo no admitidas",
    u8"regras de arredondamento n\u00e3o suportadas",
    u8"r\u00e8gles d'arrondi non prises en charge",
    u8"nicht unterst\u00fctzte Rundungsregeln",
    u8"\u043d\u0435\u043f\u043e\u0434\u0434\u0435\u0440\u0436\u0438\u0432\u0430\u0435\u043c\u044b\u0435 \u043f\u0440\u0430\u0432\u0438\u043b\u0430 \u043e\u043a\u0440\u0443\u0433\u043b\u0435\u043d\u0438\u044f",
    u8"\u672a\u5bfe\u5fdc\u306e\u4e38\u3081\u898f\u5247",
    u8"\uc9c0\uc6d0\ub418\uc9c0 \uc54a\ub294 \ubc18\uc62c\ub9bc \uaddc\uce59",
    u8"\u0642\u0648\u0627\u0639\u062f \u0627\u0644\u062a\u0642\u0631\u064a\u0628 \u063a\u064a\u0631 \u0627\u0644\u0645\u062f\u0639\u0648\u0645\u0629",
    u8"\u0905\u0938\u092e\u0930\u094d\u0925\u093f\u0924 \u092a\u0942\u0930\u094d\u0923\u093e\u0902\u0915\u0928 \u0928\u093f\u092f\u092e",
    u8"\u0985\u09b8\u09ae\u09b0\u09cd\u09a5\u09bf\u09a4 \u09b0\u09be\u0989\u09a8\u09cd\u09a1\u09bf\u0982 \u09a8\u09bf\u09af\u09bc\u09ae",
    u8"aturan pembulatan yang tidak didukung",
    u8"desteklenmeyen yuvarlama kurallar\u0131",
    u8"quy t\u1eafc l\u00e0m tr\u00f2n ch\u01b0a h\u1ed7 tr\u1ee3"
}};
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
std::vector<std::string> keys(std::string_view text){std::vector<std::string> result;std::size_t at=0;
    while((at=text.find('[',at))!=text.npos){const auto end=text.find(']',at);CHECK(end!=text.npos);if(end==text.npos)break;result.emplace_back(text.substr(at,end-at+1));at=end+1;}return result;}
void catalog(){
    CHECK(i18n::tariffPanelMessageKeyCount==27&&i18n::tariffPanelLocalePacks().size()==16);std::set<std::string> identities;
    for(std::size_t key=0;key<27;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::tariffPanelMessageKeyName(k))).second&&i18n::tariffPanelMessageKeyName(k).find("tariff.panel.")==0&&i18n::tariffPanelEnglish(k)==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::tariffPanelLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<27;++key){const auto message=i18n::tariffPanelMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            CHECK(i18n::templateParameters(message.text,&parameters)&&parameters.empty());
            if(static_cast<Key>(key)==Key::Excludes)CHECK(message.text.find(roundingExclusions[p])!=message.text.npos);
            if(key==16)CHECK(message.text.substr(0,2)==" ["&&message.text.back()==']'); // human annotation, not a keyboard key
            else CHECK(keys(english[key])==keys(message.text));
            for(const auto *token:{"12","Ctrl+C","OFFLINE DECLARATION","Q/D/L/S","tariff-declaration.json","ONLY","NOT","DECLARED FORECAST","<=370","EXCLUDES","USD","->"})if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::tariffPanelMessageKeyName(Key::Count).empty()&&i18n::tariffPanelMessage("en",Key::Count).text.empty()&&i18n::tariffPanelEnglish(Key::Count).empty());
}
std::size_t memberIndex(const JsonValue &value,const std::string &name){for(std::size_t i=0;i<value.members.size();++i)if(value.members[i].first==name)return i;throw 1;}
void native(){
    const auto empty=tariffDeclarationTemplate();const auto before=tariffJson(empty);TariffSettingsPanel panel;panel.open(empty);
    const auto rendered=[&]{return panel.render(1024,1000,ColorLevel::None);};
    const auto has=[&](Key key){const auto text=rendered();CHECK(text.find(english[static_cast<std::size_t>(key)])!=text.npos);};
    CHECK(panel.render(80,11,ColorLevel::None)==english[0]+"\n"+english[1]+"\n"+english[2]+"\n");
    has(Key::Title);has(Key::OfflineDefault);has(Key::FooterBrowse);has(Key::FixedSuffix);
    panel.field=memberIndex(panel.draft,"plan");has(Key::FieldsPrompt);
    panel.page=TariffSettingsPage::Edit;panel.field=memberIndex(panel.draft,"country");panel.input="qDLS";has(Key::EditorKeys);has(Key::FooterEdit);
    panel.page=TariffSettingsPage::Leave;has(Key::LeaveTitle);has(Key::LeaveKeys);
    panel.page=TariffSettingsPage::Remove;has(Key::RemoveKeys);has(Key::RemovePreserve);
    panel.open(empty);const auto plan=memberIndex(panel.draft,"plan"),revisions=memberIndex(panel.draft.members[plan].second,"revisions");
    panel.draft.members[plan].second.members[revisions].second.items.clear();panel.route={plan,revisions};has(Key::EmptyTable);
    panel.open(empty);panel.draft.members[memberIndex(panel.draft,"country")].second.text="synthetic-change";has(Key::UnsavedSuffix);CHECK(rendered().find(english[13]+english[14]+"\n")!=std::string::npos);
    panel.open(empty);panel.key('v',{},1800000000);
    for(std::size_t key=17;key<27;++key)CHECK(std::find(panel.review.begin(),panel.review.end(),english[key])!=panel.review.end());
    has(Key::FooterReview);CHECK(tariffJson(panel.active)==before&&tariffJson(panel.draft)==before&&panel.page==TariffSettingsPage::Review);
    CHECK(panel.key(3,{},1800000000)==TariffSettingsResult::Quit);
}
void matrix(){
    for(const auto &pack:i18n::tariffPanelLocalePacks())for(std::size_t key=0;key<27;++key){TerminalTariffPanelMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<Key>(key);request.literalTone=Tone::Green;
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalTariffPanelMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size()&&line.metadata.sources[run.content.sourceSpan].empty()&&run.tone==Tone::Green);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalTariffPanelMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover))CHECK(unisolate(joined)==i18n::tariffPanelMessage(recover?"en":pack.id,request.key).text&&!line.line.line.clipped);
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalTariffPanelMessage(request,policy,128,&rows)&&renderWrappedTerminalTariffPanelMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<10;++mode){TerminalTariffPanelMessageRequest bad;TerminalTextPolicy policy;policy.order=order;
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
        CHECK(!layoutTerminalTariffPanelMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalTariffPanelMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalTariffPanelMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalTariffPanelMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    TerminalTariffPanelMessageRequest request;request.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalTariffPanelMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    policy.columns=3;request.key=Key::Excludes;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalTariffPanelMessage(request,policy,1,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalTariffPanelMessage({},policy,nullptr)&&!wrapTerminalTariffPanelMessage({},policy,1,nullptr)&&!renderTerminalTariffPanelMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalTariffPanelMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack literal offline tariff shell and review limitations without supplier, reset, billing or activation authority");}
