// Human pool-declaration labels only; no endpoint, consent input, TLS factory or donation authority.
#include "product/terminal_settings_message.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::PoolFieldMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::poolFieldMessageKeyCount==8&&i18n::poolFieldLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,8> english={{"Scope","Mode","TLS URL","TCP URL","Plaintext consent","Add user pool","Remove user pool","Invalid"}};
    for(std::size_t key=0;key<8;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::poolFieldMessageKeyName(k))).second&&i18n::poolFieldMessageKeyName(k).find("pool.field.")==0&&i18n::poolFieldEnglish(k)==english[key]);}
    for(std::size_t field=0;field<7;++field)CHECK(i18n::poolFieldEnglish(i18n::poolFieldKeys()[field])==english[field]);
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::poolFieldLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<8;++key){const auto message=i18n::poolFieldMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            CHECK(i18n::templateParameters(message.text,&parameters)&&parameters.empty());
            for(const auto *token:{"TLS","TCP","URL"})if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::poolFieldMessageKeyName(Key::Count).empty()&&i18n::poolFieldMessage("en",Key::Count).text.empty()&&i18n::poolFieldEnglish(Key::Count).empty());
}
void matrix(){
    for(const auto &pack:i18n::poolFieldLocalePacks())for(std::size_t key=0;key<8;++key){TerminalPoolFieldMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<Key>(key);request.literalTone=Tone::Green;
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalPoolFieldMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size()&&line.metadata.sources[run.content.sourceSpan].empty()&&run.tone==Tone::Green);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalPoolFieldMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover))CHECK(unisolate(joined)==i18n::poolFieldMessage(recover?"en":pack.id,request.key).text&&!line.line.line.clipped);
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalPoolFieldMessage(request,policy,128,&rows)&&renderWrappedTerminalPoolFieldMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<8;++mode){TerminalPoolFieldMessageRequest bad;TerminalTextPolicy policy;policy.order=order;
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
        CHECK(!layoutTerminalPoolFieldMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalPoolFieldMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalPoolFieldMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalPoolFieldMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    TerminalPoolFieldMessageRequest request;request.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalPoolFieldMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    policy.columns=3;request.key=Key::PlaintextConsent;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalPoolFieldMessage(request,policy,1,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalPoolFieldMessage({},policy,nullptr)&&!wrapTerminalPoolFieldMessage({},policy,1,nullptr)&&!renderTerminalPoolFieldMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalPoolFieldMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();matrix();failures();return finishTests("sixteen-pack pool declaration labels without TLS, consent or donation authority");}
