// Literal pool-shell/editor/security warnings only; no endpoints, credentials or permission in display.
#include "product/terminal_settings_message.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::PoolPanelMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
std::vector<std::string> keys(std::string_view text){std::vector<std::string> result;std::size_t at=0;
    while((at=text.find('[',at))!=text.npos){const auto end=text.find(']',at);CHECK(end!=text.npos);if(end==text.npos)break;result.emplace_back(text.substr(at,end-at+1));at=end+1;}return result;}
void catalog(){
    CHECK(i18n::poolPanelMessageKeyCount==30&&i18n::poolPanelLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,30> english={{"Terminal too short.","Resize to 12+ rows.","[Ctrl+C] Stop [Esc] Back","[Enter] Draft [Esc] Cancel [Ctrl+C] Stop","[Arrows] Scroll [Enter] Save [Esc] Edit [Q] Stop","[Tab] Field [Enter] Change [V] Review [Esc] Back [Q] Stop","PLAINTEXT RISK","Pools / TLS declarations"," *","Login/proofs exposed. MITM can trigger it.","Remove selected user declaration only; no live pool changes.","Explicit DNS endpoint only; no password, wallet, path or inferred port.","Type ALLOW PLAINTEXT","Type REMOVE POOL.","Q/D/L/S are input; Esc cancels.","Unsaved pool declarations","[Enter] Review [D] Discard [Esc] Continue","Review pool-security-preferences.json only","No live activation; requested TLS refuses the next ordinary mining start until the runtime is qualified.","Certificate, hostname, trust, unknown and protocol failures NEVER downgrade. Explicit strict SSL URLs never fall back.","Plaintext exposes login and proofs. Network-error fallback is NOT downgrade resistant.","Before: absent","After: removed","Donation - independent scope; no inherited endpoint or consent","Current session and its connections stay unchanged.","No active session; Save starts no networking.","No published candidate endpoint is treated as verified by this declaration. F2Pool/donation TLS must not be guessed.","[End of TLS review]","Configured only; TLS runtime unavailable. User and donation scopes never inherit permissions.","Declarations only; no live connection."}};
    for(std::size_t key=0;key<30;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::poolPanelMessageKeyName(k))).second&&i18n::poolPanelMessageKeyName(k).find("pool.panel.")==0&&i18n::poolPanelEnglish(k)==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::poolPanelLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<30;++key){const auto message=i18n::poolPanelMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            CHECK(i18n::templateParameters(message.text,&parameters)&&parameters.empty()&&keys(english[key])==keys(message.text));
            for(const auto *token:{"TLS","SSL","URL","DNS","MITM","ALLOW PLAINTEXT","REMOVE POOL","Q/D/L/S","Esc","pool-security-preferences.json","F2Pool","12"})
                if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::poolPanelMessageKeyName(Key::Count).empty()&&i18n::poolPanelMessage("en",Key::Count).text.empty()&&i18n::poolPanelEnglish(Key::Count).empty());
}
void matrix(){
    for(const auto &pack:i18n::poolPanelLocalePacks())for(std::size_t key=0;key<30;++key){TerminalPoolPanelMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<Key>(key);request.literalTone=Tone::Green;
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalPoolPanelMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size()&&line.metadata.sources[run.content.sourceSpan].empty()&&run.tone==Tone::Green);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalPoolPanelMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover))CHECK(unisolate(joined)==i18n::poolPanelMessage(recover?"en":pack.id,request.key).text&&!line.line.line.clipped);
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalPoolPanelMessage(request,policy,128,&rows)&&renderWrappedTerminalPoolPanelMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<8;++mode){TerminalPoolPanelMessageRequest bad;TerminalTextPolicy policy;policy.order=order;
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
        CHECK(!layoutTerminalPoolPanelMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalPoolPanelMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalPoolPanelMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalPoolPanelMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    TerminalPoolPanelMessageRequest request;request.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalPoolPanelMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    policy.columns=3;request.key=Key::NoDowngrade;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalPoolPanelMessage(request,policy,1,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalPoolPanelMessage({},policy,nullptr)&&!wrapTerminalPoolPanelMessage({},policy,1,nullptr)&&!renderTerminalPoolPanelMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalPoolPanelMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();matrix();failures();return finishTests("sixteen-pack literal pool shell and security limits without networking or downgrade authority");}
