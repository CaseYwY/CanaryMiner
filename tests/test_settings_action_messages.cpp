// Root action/limitation text only. Reading or rendering a message cannot activate its backend.
#include "product/terminal_settings_message.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::SettingsActionMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
std::vector<std::string> keys(std::string_view text){std::vector<std::string> result;std::size_t at=0;
    while((at=text.find('[',at))!=text.npos){const auto end=text.find(']',at);CHECK(end!=text.npos);if(end==text.npos)break;result.emplace_back(text.substr(at,end-at+1));at=end+1;}return result;}
void catalogs(){
    CHECK(i18n::settingsActionMessageKeyCount==34&&i18n::settingsActionLocalePacks().size()==16);std::set<std::string> identities;
    const auto &source=i18n::settingsActionLocalePacks()[0];std::size_t enter=0;
    for(std::size_t key=0;key<34;++key){const auto k=static_cast<Key>(key);const auto identity=i18n::settingsActionMessageKeyName(k);
        CHECK(identity.find("settings.")==0&&identities.insert(std::string(identity)).second);
        if(key==0){CHECK(i18n::settingsActionEnglish(k,"x")=="Hide worker label  [x]"&&i18n::settingsActionEnglish(k," ")=="Hide worker label  [ ]");
            CHECK(i18n::settingsActionEnglish(k).empty()&&i18n::settingsActionEnglish(k,"xx").empty()&&i18n::settingsActionEnglish(k,u8"١").empty());}
        else CHECK(i18n::settingsActionEnglish(k)==source.messages[key]&&i18n::settingsActionEnglish(k,"x").empty());
        if(source.messages[key].find("[Enter]")!=source.messages[key].npos)++enter;
    }
    CHECK(enter==13);
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::settingsActionLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<34;++key){const auto message=i18n::settingsActionMessage(pack.id,static_cast<Key>(key));
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl);
            CHECK(i18n::validTemplateTranslation(source.messages[key],message.text)&&keys(message.text)==keys(source.messages[key]));
            for(const auto *token:{"PRL/USDT","FX","TLS","CLI","API","URL","1h","docs/feature-roadmap.md"})if(source.messages[key].find(token)!=source.messages[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::settingsActionEnglish(Key::LanguageEffective)=="Effective English/legacy formatting remains active.");
    CHECK(i18n::settingsActionEnglish(Key::PoolRefusal)=="No live activation; requested TLS refuses ordinary startup.");
    CHECK(i18n::settingsActionEnglish(Key::GameRefusal)=="Non-off requests refuse the next ordinary start.");
    CHECK(i18n::settingsActionEnglish(Key::PrivacyNotAnonymized)=="Logs, API and files are not anonymized by it.");
    CHECK(i18n::settingsActionEnglish(Key::RawDrain)=="Raw lines remain unclassified; filtering does not stop pipe draining.");
    CHECK(i18n::settingsActionMessageKeyName(Key::Count).empty()&&i18n::settingsActionMessage("en",Key::Count).text.empty()&&i18n::settingsActionEnglish(Key::Count).empty());
    CHECK(i18n::settingsActionEnglish(Key::HideWorker,std::string(4097,'x')).empty()&&i18n::settingsActionEnglish(Key::HideWorker,"\x1b").empty());
}
TerminalSettingsActionMessageRequest requestFor(const i18n::SettingsActionLocalePack &pack,Key key,const char *state="x"){
    TerminalSettingsActionMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;
    if(key==Key::HideWorker)request.arguments={{"state",{{state,{},true},Tone::Canary}}};
    return request;
}
void matrix(){
    for(const auto &pack:i18n::settingsActionLocalePacks())for(std::size_t key=0;key<34;++key)for(unsigned variant=0;variant<(key==0?2u:1u);++variant){const auto request=requestFor(pack,static_cast<Key>(key),variant?" ":"x");
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalSettingsActionMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            CHECK(line.metadata.asciiRecovery==(order==TerminalTextOrder::Ascii)&&line.metadata.backendFallback==(order!=TerminalTextOrder::Ascii&&!terminalTextBackend().unicode));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());CHECK(run.tone==(line.metadata.sources[run.content.sourceSpan].empty()?Tone::Green:Tone::Canary));}}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalSettingsActionMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){auto expected=std::string(i18n::settingsActionMessage(recover?"en":pack.id,request.key).text);
                if(!request.arguments.empty())expected.replace(expected.find("{state}"),7,request.arguments[0].value.content.text);
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
    }
}
void rows(){
    for(const auto &pack:i18n::settingsActionLocalePacks())for(std::size_t key=0;key<34;++key)for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
        const auto request=requestFor(pack,static_cast<Key>(key));TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
        CHECK(wrapTerminalSettingsActionMessage(request,policy,128,&rows)&&renderWrappedTerminalSettingsActionMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
        CHECK(rows.metadata.sources==painted.metadata.sources&&rows.metadata.renderedLanguage==painted.metadata.renderedLanguage);
        for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);
    }
}
void failures(){
    const auto &pack=i18n::settingsActionLocalePacks()[0];auto request=requestFor(pack,Key::HideWorker);
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<16;++mode){auto bad=request;TerminalTextPolicy policy;policy.order=order;policy.columns=79;
        if(mode==0)bad.key=Key::Count;
        if(mode==1)bad.arguments.clear();
        if(mode==2)bad.arguments.push_back(bad.arguments[0]);
        if(mode==3)bad.arguments[0].name="unexpected";
        if(mode==4)bad.arguments[0].value.content.invariant=false;
        if(mode==5)bad.arguments[0].value.content.text="xx";
        if(mode==6)bad.arguments[0].value.content.text="\x1b";
        if(mode==7)bad.arguments[0].value.content.ascii="not-the-mark";
        if(mode==8)bad.language="en@private";
        if(mode==9)bad.literalTone=static_cast<Tone>(99);
        if(mode==10)bad.arguments[0].value.tone=static_cast<Tone>(99);
        if(mode==11)bad.arguments[0].value.content.text=std::string(4097,'x');
        if(mode==12)bad.arguments[0].value.content.ascii=std::string(4097,'x');
        if(mode==13)bad.arguments[0].name=std::string(33,'x');
        if(mode==14)bad.arguments.resize(9,bad.arguments[0]);
        if(mode==15)bad.key=Key::PoolRefusal;
        TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
        PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
        CHECK(!layoutTerminalSettingsActionMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalSettingsActionMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalSettingsActionMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalSettingsActionMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});
    }
    TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;request.language="en-US";TerminalMessageLine line;
    CHECK(layoutTerminalSettingsActionMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    request=requestFor(pack,Key::PrivacyPresentation);policy.columns=3;PaintedTerminalMessageRows painted;painted.rows={"previous"};
    CHECK(!renderWrappedTerminalSettingsActionMessage(request,policy,1,ColorLevel::None,&painted)&&painted.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalSettingsActionMessage({},policy,nullptr)&&!wrapTerminalSettingsActionMessage({},policy,1,nullptr)&&!renderTerminalSettingsActionMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalSettingsActionMessage({},policy,1,ColorLevel::None,nullptr));
}
}
int main(){catalogs();matrix();rows();failures();return finishTests("sixteen-pack root actions and capability notices with source-only native binding");}
