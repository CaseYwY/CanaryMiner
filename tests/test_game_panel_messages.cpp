// Literal game-shell notices only. No target input, detector, private identity or runtime activation.
#include "product/terminal_settings_message.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::GamePanelMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
std::vector<std::string> keys(std::string_view text){std::vector<std::string> result;std::size_t at=0;
    while((at=text.find('[',at))!=text.npos){const auto end=text.find(']',at);CHECK(end!=text.npos);if(end==text.npos)break;result.emplace_back(text.substr(at,end-at+1));at=end+1;}return result;}
void catalog(){
    CHECK(i18n::gamePanelMessageKeyCount==33&&i18n::gamePanelLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,33> english={{"Use 1..128 ASCII basename characters, not a path or pattern.","Enter an integer 0..30000 ms.","Enter an integer 100..10000 ms.","Enter an integer 100..30000 ms.","Saved game defaults; no runtime activation.","Save failed; draft retained.","Terminal too short.","Resize to 12+ rows.","[Ctrl+C] Stop [Esc] Back","[Enter] Draft [Esc] Cancel [Ctrl+C] Stop","[Arrows] Scroll [Enter] Save [Esc] Edit [Q] Stop","[Tab] Field [Enter] Change [V] Review [Esc] Back [Q] Stop","Games / saved defaults"," *","Q/D/L/S are input. Esc cancels this field.","Unsaved game defaults","[Enter] Review [D] Discard [Esc] Continue","Review game-preferences.json only","Next explicit parent start only.","Native safety/CLI binding unavailable.","A non-off request refuses startup.","Current session (frozen):","Current session: effective defaults unavailable.","No active session.","Scope is explicit; foreign platforms cannot imply game exit. Unknown detection holds work.","Coexist requires real baseline/experience and economic qualification. Ten-minute nonpositive-profit lock remains on.","Current session, its workers and game episode remain frozen. No scan or game starts from Save.","[End of game defaults]","Effective: unavailable","No active session","Configured values only. Native safety/CLI activation unavailable; non-off refuses the next ordinary start.","Exact basename and scope; incomplete detection stays unknown. Coexist still requires qualification.","Private defaults; no live activation."}};
    for(std::size_t key=0;key<33;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::gamePanelMessageKeyName(k))).second&&i18n::gamePanelMessageKeyName(k).find("game.panel.")==0&&i18n::gamePanelEnglish(k)==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::gamePanelLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<33;++key){const auto message=i18n::gamePanelMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            CHECK(i18n::templateParameters(message.text,&parameters)&&parameters.empty()&&keys(english[key])==keys(message.text));
            for(const auto *token:{"ASCII","game-preferences.json","Q/D/L/S","Esc","CLI","off","Coexist","1..128","0..30000","100..10000","100..30000","12"})
                if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);
            // ms is a unit in these three timing prompts, not the suffix of the English word platforms.
            if(key>=1&&key<=3)CHECK(message.text.find("ms")!=message.text.npos);}}
    CHECK(i18n::gamePanelMessageKeyName(Key::Count).empty()&&i18n::gamePanelMessage("en",Key::Count).text.empty()&&i18n::gamePanelEnglish(Key::Count).empty());
}
void matrix(){
    for(const auto &pack:i18n::gamePanelLocalePacks())for(std::size_t key=0;key<33;++key){TerminalGamePanelMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<Key>(key);request.literalTone=Tone::Green;
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalGamePanelMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size()&&line.metadata.sources[run.content.sourceSpan].empty()&&run.tone==Tone::Green);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalGamePanelMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover))CHECK(unisolate(joined)==i18n::gamePanelMessage(recover?"en":pack.id,request.key).text&&!line.line.line.clipped);
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalGamePanelMessage(request,policy,128,&rows)&&renderWrappedTerminalGamePanelMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<8;++mode){TerminalGamePanelMessageRequest bad;TerminalTextPolicy policy;policy.order=order;
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
        CHECK(!layoutTerminalGamePanelMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalGamePanelMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalGamePanelMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalGamePanelMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    TerminalGamePanelMessageRequest request;request.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalGamePanelMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    policy.columns=3;request.key=Key::CoexistQualification;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalGamePanelMessage(request,policy,1,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalGamePanelMessage({},policy,nullptr)&&!wrapTerminalGamePanelMessage({},policy,1,nullptr)&&!renderTerminalGamePanelMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalGamePanelMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();matrix();failures();return finishTests("sixteen-pack literal game shell/editor notices with unchanged private-default and refusal boundaries");}
