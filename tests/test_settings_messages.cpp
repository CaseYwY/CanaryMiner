// Settings shell extraction only; no field/subpanel coverage or native translation activation is implied.
#include "product/terminal_settings_message.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
std::vector<std::string> keys(std::string_view text){std::vector<std::string> result;std::size_t at=0;
    while((at=text.find('[',at))!=text.npos){const auto end=text.find(']',at);CHECK(end!=text.npos);if(end==text.npos)break;result.emplace_back(text.substr(at,end-at+1));at=end+1;}
    std::sort(result.begin(),result.end());return result;}
void catalogs(){
    CHECK(i18n::settingsMessageKeyCount==30&&i18n::settingsLocalePacks().size()==16&&i18n::settingsGroupKeys().size()==11);
    std::set<std::string> identities;
    for(std::size_t key=0;key<i18n::settingsMessageKeyCount;++key){const auto identity=i18n::settingsMessageKeyName(static_cast<i18n::SettingsMessageKey>(key));
        CHECK(identity.find("settings.")==0&&identities.insert(std::string(identity)).second);}
    const auto &source=i18n::settingsLocalePacks()[0];
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::settingsLocalePacks()[p];CHECK(pack.id==i18n::localePacks()[p].id&&pack.review==i18n::localePacks()[p].review&&pack.rtl==i18n::localePacks()[p].rtl);
        for(std::size_t key=0;key<i18n::settingsMessageKeyCount;++key){const auto text=i18n::settingsMessage(pack.id,static_cast<i18n::SettingsMessageKey>(key));
            CHECK(!text.text.empty()&&!text.fallback&&text.locale==pack.id&&text.review==pack.review&&text.rtl==pack.rtl);
            CHECK(i18n::validTemplateTranslation(source.messages[key],text.text)&&keys(source.messages[key])==keys(text.text));}
    }
    CHECK(i18n::settingsMessageKeyName(i18n::SettingsMessageKey::Count).empty()&&i18n::settingsMessage("en",i18n::SettingsMessageKey::Count).text.empty());
    CHECK(i18n::settingsEnglishField(i18n::SettingsMessageKey::ReviewHeading,"1")=="Review 1 changes / arrows scroll");
    CHECK(i18n::settingsEnglishField(i18n::SettingsMessageKey::GroupHeading,"Privacy")=="< Privacy >  (Left/Right: group)");
    CHECK(i18n::settingsEnglishField(i18n::SettingsMessageKey::GroupHeading,"{group}")=="< {group} >  (Left/Right: group)");
    CHECK(i18n::settingsEnglish(i18n::SettingsMessageKey::Saved)=="Saved. Presentation changes are live; mining settings were not changed.");
    CHECK(i18n::settingsEnglish(i18n::SettingsMessageKey::SaveFailed)=="Save failed; active settings are unchanged.");
    CHECK(i18n::settingsEnglishField(i18n::SettingsMessageKey::Title,"x").empty()&&i18n::settingsEnglishField(i18n::SettingsMessageKey::ReviewHeading,std::string(4096,'x')).empty());
    CHECK(i18n::settingsEnglishField(i18n::SettingsMessageKey::GroupHeading,u8"隐私").empty()&&i18n::settingsEnglishField(i18n::SettingsMessageKey::GroupHeading,"\x1b[31m").empty());
}
void matrix(){
    for(const auto &pack:i18n::settingsLocalePacks())for(std::size_t key=0;key<i18n::settingsMessageKeyCount;++key)
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})
            for(std::size_t width:{0u,3u,19u,79u,1024u}){
                TerminalSettingsMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<i18n::SettingsMessageKey>(key);request.literalTone=Tone::Green;
                if(request.key==i18n::SettingsMessageKey::ReviewHeading)request.arguments={{"count",{{"7",{},false,TerminalTextAtom::LeftToRight},Tone::Canary}}};
                if(request.key==i18n::SettingsMessageKey::GroupHeading)request.arguments={{"group",{{std::string(i18n::settingsMessage(pack.id,i18n::SettingsMessageKey::Privacy).text),"Privacy",false},Tone::Canary}}};
                TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
                CHECK(layoutTerminalSettingsMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
                CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id));
                CHECK(line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review)&&line.metadata.asciiRecovery==(order==TerminalTextOrder::Ascii)&&line.metadata.backendFallback==(order!=TerminalTextOrder::Ascii&&!terminalTextBackend().unicode));
                std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                    if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                    else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());CHECK(run.tone==(line.metadata.sources[run.content.sourceSpan].empty()?Tone::Green:Tone::Canary));}}
                CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
                PaintedTerminalMessageLine painted;CHECK(renderTerminalSettingsMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
                if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){auto expected=std::string(i18n::settingsMessage(recover?"en":pack.id,request.key).text);
                    if(!request.arguments.empty()){const auto marker=request.key==i18n::SettingsMessageKey::ReviewHeading?"{count}":"{group}";const auto at=expected.find(marker);
                        const auto &value=request.arguments[0].value.content;expected.replace(at,std::char_traits<char>::length(marker),recover?(value.ascii.empty()?value.text:value.ascii):value.text);}
                    CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
            }
}
void rowsAndFailures(){
    for(const auto &pack:i18n::settingsLocalePacks())for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
        TerminalSettingsMessageRequest request;request.language=std::string(pack.id);request.key=i18n::SettingsMessageKey::SeparateSaves;TerminalTextPolicy policy;policy.order=order;policy.columns=19;
        TerminalMessageRows rows;PaintedTerminalMessageRows painted;
        CHECK(wrapTerminalSettingsMessage(request,policy,128,&rows)&&renderWrappedTerminalSettingsMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
        for(std::size_t i=0;i<rows.rows.size();++i)CHECK(painted.rows[i]==rows.rows[i].line.text&&!rows.rows[i].line.clipped);
    }
    TerminalSettingsMessageRequest request;request.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalSettingsMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    for(unsigned mode=0;mode<12;++mode){auto bad=request;bad.key=i18n::SettingsMessageKey::ReviewHeading;bad.arguments={{"count",{{"7"},Tone::Canary}}};
        if(mode==0)bad.arguments.clear();
        if(mode==1)bad.arguments.push_back(bad.arguments[0]);
        if(mode==2)bad.arguments[0].name="unexpected";
        if(mode==3)bad.arguments[0].value.content.text="\x1b";
        if(mode==4)bad.key=i18n::SettingsMessageKey::Count;
        if(mode==5)bad.language="en@private";
        if(mode==6)bad.literalTone=static_cast<Tone>(99);
        if(mode==7)bad.language=std::string(36,'x');
        if(mode==8)bad.arguments.resize(9,bad.arguments[0]);
        if(mode==9)bad.arguments[0].value.content.text=std::string(4097,'x');
        if(mode==10)bad.arguments[0].value.content.ascii=std::string(4097,'x');
        if(mode==11)bad.arguments[0].name=std::string(33,'x');
        line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";
        CHECK(!layoutTerminalSettingsMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");}
    request.key=i18n::SettingsMessageKey::SeparateSaves;policy.columns=3;PaintedTerminalMessageRows rows;rows.rows={"previous"};rows.metadata.renderedLanguage="unchanged";
    CHECK(!renderWrappedTerminalSettingsMessage(request,policy,1,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"}&&rows.metadata.renderedLanguage=="unchanged");
    CHECK(!layoutTerminalSettingsMessage({},policy,nullptr)&&!wrapTerminalSettingsMessage({},policy,1,nullptr)&&!renderTerminalSettingsMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalSettingsMessage({},policy,1,ColorLevel::None,nullptr));
}
}
int main(){catalogs();matrix();rowsAndFailures();return finishTests("sixteen-pack settings shell with typed templates and source-only native extraction");}
