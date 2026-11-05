// Human local-declaration notices only. No endpoint inspection, network, consent or deletion authority in display.
#include "product/terminal_settings_message.h"
#include "product/pool_security_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::PoolNoticeMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::poolNoticeMessageKeyCount==10&&i18n::poolNoticeLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,10> english={{"Draft changed only; endpoint changes revoke prior plaintext consent.","Type ALLOW PLAINTEXT only after configuring prefer TLS and both endpoints.","Type REMOVE POOL for a user slot; at least one must remain.","Use an explicit DNS URL with the correct stratum scheme and port; no credentials or paths.","Saved declarations only; no connection or runtime activation.","Save failed; active declarations unchanged.","At most eight independent user pools.","Plaintext consent revoked in draft.","Cannot remove donation scope or the last user slot.","Invalid user slot count."}};
    for(std::size_t key=0;key<10;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::poolNoticeMessageKeyName(k))).second&&i18n::poolNoticeMessageKeyName(k).find("pool.notice.")==0&&i18n::poolNoticeEnglish(k)==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::poolNoticeLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<10;++key){const auto message=i18n::poolNoticeMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            CHECK(i18n::templateParameters(message.text,&parameters)&&parameters.empty());
            for(const auto *token:{"ALLOW PLAINTEXT","REMOVE POOL","TLS","DNS","URL","stratum"})if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::poolNoticeMessageKeyName(Key::Count).empty()&&i18n::poolNoticeMessage("en",Key::Count).text.empty()&&i18n::poolNoticeEnglish(Key::Count).empty());
}
void native(){
    PoolSecuritySettingsPanel panel;
    const auto edit=[&](unsigned field,const std::string &input){panel.open({});panel.field=field;panel.key('\r');panel.input=input;panel.key('\r');};
    edit(2,"stratum+ssl://fixture.invalid:1");CHECK(panel.message==i18n::poolNoticeEnglish(Key::DraftChanged)&&panel.draft.users[0].tlsUrl=="stratum+ssl://fixture.invalid:1"&&!panel.draft.users[0].plaintextConsent&&panel.active.users[0].tlsUrl.empty());
    edit(4,"ALLOW PLAINTEXT");CHECK(panel.message==i18n::poolNoticeEnglish(Key::InvalidConsent)&&!panel.draft.users[0].plaintextConsent&&panel.page==PoolSecuritySettingsPage::Edit);
    PoolSecurityPreferences multiple;multiple.users.resize(2);panel.open(multiple);panel.field=6;panel.key('\r');panel.input="REMOVE pooL";panel.key('\r');
    CHECK(panel.message==i18n::poolNoticeEnglish(Key::InvalidRemove)&&panel.draft.users.size()==2);
    edit(2,"https://fixture.invalid:1");CHECK(panel.message==i18n::poolNoticeEnglish(Key::InvalidEndpoint)&&panel.draft.users[0].tlsUrl.empty());
    panel.open({});panel.key('v');unsigned saves=0;panel.key('\r',[&](const PoolSecurityPreferences &,std::string *){++saves;return true;});
    CHECK(saves==1&&panel.message==i18n::poolNoticeEnglish(Key::Saved)&&!panel.dirty());
    panel.key('v');panel.key('\r',[](const PoolSecurityPreferences &,std::string *){return false;});CHECK(panel.message==i18n::poolNoticeEnglish(Key::SaveFailed));
    panel.key('\r',[](const PoolSecurityPreferences &,std::string *error){*error="fixture untouched error";return false;});CHECK(panel.message=="fixture untouched error");
    multiple.users.resize(8);panel.open(multiple);panel.field=5;panel.key('\r');CHECK(panel.message==i18n::poolNoticeEnglish(Key::AtMostEight)&&panel.draft.users.size()==8);
    multiple.users={{PoolSecurityMode::PreferTls,"stratum+ssl://fixture.invalid:1","stratum+tcp://fixture.invalid:2",true}};
    panel.open(multiple);panel.field=4;panel.key('\r');CHECK(panel.message==i18n::poolNoticeEnglish(Key::ConsentRevoked)&&!panel.draft.users[0].plaintextConsent&&panel.active.users[0].plaintextConsent&&!panel.draft.donation.plaintextConsent);
    panel.open({});panel.field=6;panel.key('\r');CHECK(panel.message==i18n::poolNoticeEnglish(Key::CannotRemove)&&panel.draft.users.size()==1);
    panel.donation=true;panel.key('\r');CHECK(panel.message==i18n::poolNoticeEnglish(Key::CannotRemove)&&panel.draft.users.size()==1);
    multiple.users.clear();panel.open(multiple);panel.key('v');CHECK(panel.message==i18n::poolNoticeEnglish(Key::InvalidUserCount)&&panel.page==PoolSecuritySettingsPage::Browse);
    CHECK(panel.key(3)==PoolSecuritySettingsResult::Quit);
}
void matrix(){
    for(const auto &pack:i18n::poolNoticeLocalePacks())for(std::size_t key=0;key<10;++key){TerminalPoolNoticeMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<Key>(key);request.literalTone=Tone::Green;
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalPoolNoticeMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size()&&line.metadata.sources[run.content.sourceSpan].empty()&&run.tone==Tone::Green);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalPoolNoticeMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover))CHECK(unisolate(joined)==i18n::poolNoticeMessage(recover?"en":pack.id,request.key).text&&!line.line.line.clipped);
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalPoolNoticeMessage(request,policy,128,&rows)&&renderWrappedTerminalPoolNoticeMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<8;++mode){TerminalPoolNoticeMessageRequest bad;TerminalTextPolicy policy;policy.order=order;
        if(mode==0)bad.key=Key::Count;
        if(mode==1)bad.language="en@private";
        if(mode==2)bad.language="";
        if(mode==3)bad.language=std::string(36,'x');
        if(mode==4)bad.literalTone=static_cast<Tone>(99);
        if(mode==5)policy.columns=4097;
        if(mode==6)policy.direction=static_cast<TerminalTextDirection>(99);
        if(mode==7)policy.ambiguousCells=0;
        TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
        PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
        CHECK(!layoutTerminalPoolNoticeMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalPoolNoticeMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalPoolNoticeMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalPoolNoticeMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    TerminalPoolNoticeMessageRequest request;request.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalPoolNoticeMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    policy.columns=3;request.key=Key::InvalidEndpoint;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalPoolNoticeMessage(request,policy,1,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalPoolNoticeMessage({},policy,nullptr)&&!wrapTerminalPoolNoticeMessage({},policy,1,nullptr)&&!renderTerminalPoolNoticeMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalPoolNoticeMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack pool draft/save/refusal notices without consent or networking authority");}
