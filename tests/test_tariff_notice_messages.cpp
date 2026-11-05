// Local offline declarations only; mock Save supplies no store receipt, supplier approval or billing activation.
#include "product/terminal_settings_message.h"
#include "product/tariff_settings.h"
#include "product_test_util.h"
#include <fstream>
#include <set>
using namespace om;
namespace {
using Key=i18n::TariffNoticeMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::tariffNoticeMessageKeyCount==11&&i18n::tariffNoticeLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,11> english={{"Input rejected; shorten/use ASCII or cancel. Nothing applied.","Integer only, in the field's stated units.","Draft only; V/F2 reviews and validates before Save.","Maximum 128 characters; input was not truncated into a saved value.","ASCII editor only; non-ASCII input rejected.","Removed from draft only; review before saving.","Saved offline declaration + opening ONLY. Runtime collectors and policies do not use it yet.","Save unavailable; draft retained.","Collection/document capacity reached.","Added blank row; unknown rates stay empty. Enter to edit.","Fixed schema/model contract. This field is not user-editable."}};
    for(std::size_t key=0;key<11;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::tariffNoticeMessageKeyName(k))).second&&i18n::tariffNoticeMessageKeyName(k).find("tariff.notice.")==0&&i18n::tariffNoticeEnglish(k)==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::tariffNoticeLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<11;++key){const auto message=i18n::tariffNoticeMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            CHECK(i18n::templateParameters(message.text,&parameters)&&parameters.empty());
            for(const auto *token:{"ASCII","V/F2","128","ONLY","Enter"})if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::tariffNoticeMessageKeyName(Key::Count).empty()&&i18n::tariffNoticeMessage("en",Key::Count).text.empty()&&i18n::tariffNoticeEnglish(Key::Count).empty());
}
JsonValue &member(JsonValue &value,const std::string &name){for(auto &entry:value.members)if(entry.first==name)return entry.second;throw 1;}
void enter(TariffSettingsPanel &panel,const std::string &name){auto *value=&panel.draft;for(auto i:panel.route)value=value->isObject()?&value->members[i].second:&value->items[i];
    std::size_t i=0;for(;i<value->members.size();++i)if(value->members[i].first==name)break;CHECK(i<value->members.size());panel.field=i;panel.key('\r',{},1800000000);}
void native(){
    std::ifstream file("config/tariff-declaration.example.json");const std::string text{std::istreambuf_iterator<char>(file),{}};JsonValue example;TariffProblem problem;CHECK(parseTariffDeclaration(text,&example,&problem));
    TariffSettingsPanel panel;const auto notice=[&](Key key){CHECK(panel.message==i18n::tariffNoticeEnglish(key));};const auto old=tariffJson(example);
    panel.open(example);enter(panel,"country");for(unsigned i=0;i<129;++i)panel.key('x',{},1800000000);notice(Key::TooLong);
    panel.key('\r',{},1800000000);notice(Key::InputRejected);CHECK(panel.inputRejected&&!panel.dirty()&&panel.page==TariffSettingsPage::Edit&&tariffJson(panel.draft)==old);
    panel.open(example);enter(panel,"country");panel.key(0xc3,{},1800000000);notice(Key::AsciiOnly);CHECK(!panel.dirty()&&panel.inputRejected);
    panel.open(example);enter(panel,"opening");enter(panel,"through_utc_ms");for(char c:std::string("1.5"))panel.key(c,{},1800000000);panel.key('\r',{},1800000000);notice(Key::IntegerOnly);CHECK(!panel.dirty()&&panel.page==TariffSettingsPage::Edit);
    panel.open(example);enter(panel,"country");for(char c:std::string("qDLS"))panel.key(c,{},1800000000);panel.key('\r',{},1800000000);notice(Key::DraftOnly);CHECK(panel.dirty()&&member(panel.draft,"country").text=="qDLS"&&tariffJson(panel.active)==old);
    panel.open(example);enter(panel,"schema_version");notice(Key::FixedContract);CHECK(panel.page==TariffSettingsPage::Browse&&!panel.dirty());
    panel.open(example);enter(panel,"plan");enter(panel,"revisions");panel.key('a',{},1800000000);notice(Key::AddedBlank);CHECK(member(member(panel.draft,"plan"),"revisions").items.size()==2&&tariffJson(panel.active)==old);
    panel.key('x',{},1800000000);panel.key('\r',{},1800000000);notice(Key::Removed);CHECK(!panel.dirty());
    auto full=example;auto &revisions=member(member(full,"plan"),"revisions").items;revisions.resize(16,revisions.front());panel.open(full);enter(panel,"plan");enter(panel,"revisions");panel.key('a',{},1800000000);notice(Key::Capacity);CHECK(!panel.dirty());
    panel.open(example);panel.key('v',{},1800000000);panel.key('\r',{},1800000000);notice(Key::SaveUnavailable);CHECK(panel.page==TariffSettingsPage::Review&&tariffJson(panel.active)==old);
    unsigned saves=0;auto save=[&](const JsonValue &candidate,std::string *){++saves;CHECK(candidate.find("confirmed_utc")&&candidate.find("confirmed_utc")->text=="1800000000");return true;};
    panel.open(example);panel.key('v',{},1800000000);panel.key('\r',save,1800000000);notice(Key::SavedOffline);CHECK(saves==1&&!panel.dirty());
    panel.open(example);panel.key('v',{},1800000000);auto reject=[](const JsonValue &,std::string *error){*error="synthetic raw callback refusal";return false;};panel.key('\r',reject,1800000000);CHECK(panel.message=="synthetic raw callback refusal"&&tariffJson(panel.active)==old);
    panel.open(example);enter(panel,"country");CHECK(panel.key(3,{},1800000000)==TariffSettingsResult::Quit&&saves==1);
}
void matrix(){
    for(const auto &pack:i18n::tariffNoticeLocalePacks())for(std::size_t key=0;key<11;++key){TerminalTariffNoticeMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<Key>(key);request.literalTone=Tone::Green;
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalTariffNoticeMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size()&&line.metadata.sources[run.content.sourceSpan].empty()&&run.tone==Tone::Green);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalTariffNoticeMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover))CHECK(unisolate(joined)==i18n::tariffNoticeMessage(recover?"en":pack.id,request.key).text&&!line.line.line.clipped);
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalTariffNoticeMessage(request,policy,128,&rows)&&renderWrappedTerminalTariffNoticeMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<10;++mode){TerminalTariffNoticeMessageRequest bad;TerminalTextPolicy policy;policy.order=order;
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
        CHECK(!layoutTerminalTariffNoticeMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalTariffNoticeMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalTariffNoticeMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalTariffNoticeMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    TerminalTariffNoticeMessageRequest request;request.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalTariffNoticeMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    policy.columns=3;request.key=Key::SavedOffline;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalTariffNoticeMessage(request,policy,1,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalTariffNoticeMessage({},policy,nullptr)&&!wrapTerminalTariffNoticeMessage({},policy,1,nullptr)&&!renderTerminalTariffNoticeMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalTariffNoticeMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack local offline tariff notices without supplier, reset, billing or runtime authority");}
