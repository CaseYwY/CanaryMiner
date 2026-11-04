// Language labels/review descriptions only. No machine-policy renaming, status certification or activation.
#include "product/terminal_settings_message.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::LanguageFieldMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalogs(){
    CHECK(i18n::languageFieldMessageKeyCount==11&&i18n::languageFieldLocalePacks().size()==16&&i18n::languageFieldKeys().size()==8);std::set<std::string> identities;
    const std::array<std::string,11> english={{"Language ID","Format locale","Digit style","Text order","Direction","Ambiguous cells","Emoji cells","Grouping","Invalid","English source (not externally certified)","machine-assisted draft (not human reviewed)"}};
    for(std::size_t key=0;key<11;++key){const auto k=static_cast<Key>(key);const auto identity=i18n::languageFieldMessageKeyName(k);
        CHECK(identity.find("language.")==0&&identities.insert(std::string(identity)).second&&i18n::languageFieldEnglish(k)==english[key]);
        if(key<8)CHECK(i18n::languageFieldKeys()[key]==k);}
    const auto &source=i18n::languageFieldLocalePacks()[0];
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::languageFieldLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<11;++key){const auto message=i18n::languageFieldMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl);
            CHECK(i18n::validTemplateTranslation(source.messages[key],message.text)&&i18n::templateParameters(message.text,&parameters)&&parameters.empty());}}
    CHECK(i18n::languageFieldMessageKeyName(Key::Count).empty()&&i18n::languageFieldMessage("en",Key::Count).text.empty()&&i18n::languageFieldEnglish(Key::Count).empty());
}
void matrix(){
    for(const auto &pack:i18n::languageFieldLocalePacks())for(std::size_t key=0;key<11;++key){TerminalLanguageFieldMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<Key>(key);request.literalTone=Tone::Green;
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalLanguageFieldMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            CHECK(line.metadata.asciiRecovery==(order==TerminalTextOrder::Ascii)&&line.metadata.backendFallback==(order!=TerminalTextOrder::Ascii&&!terminalTextBackend().unicode));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size()&&line.metadata.sources[run.content.sourceSpan].empty()&&run.tone==Tone::Green);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalLanguageFieldMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover))CHECK(unisolate(joined)==i18n::languageFieldMessage(recover?"en":pack.id,request.key).text&&!line.line.line.clipped);
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalLanguageFieldMessage(request,policy,128,&rows)&&renderWrappedTerminalLanguageFieldMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<8;++mode){TerminalLanguageFieldMessageRequest bad;TerminalTextPolicy policy;policy.order=order;
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
        CHECK(!layoutTerminalLanguageFieldMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalLanguageFieldMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalLanguageFieldMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalLanguageFieldMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});
    }
    TerminalLanguageFieldMessageRequest request;request.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalLanguageFieldMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    request.key=Key::SourceReview;policy.columns=3;PaintedTerminalMessageRows painted;painted.rows={"previous"};
    CHECK(!renderWrappedTerminalLanguageFieldMessage(request,policy,1,ColorLevel::None,&painted)&&painted.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalLanguageFieldMessage({},policy,nullptr)&&!wrapTerminalLanguageFieldMessage({},policy,1,nullptr)&&!renderTerminalLanguageFieldMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalLanguageFieldMessage({},policy,1,ColorLevel::None,nullptr));
}
}
int main(){catalogs();matrix();failures();return finishTests("sixteen-pack language field labels and honest source/draft review descriptions");}
