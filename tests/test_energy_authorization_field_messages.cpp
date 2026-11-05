// Human persistence labels only; no saved authorization, private identity, helper receipt or capture-plan facts.
#include "product/terminal_settings_message.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::EnergyAuthorizationFieldMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::energyAuthorizationFieldMessageKeyCount==14&&i18n::energyAuthorizationFieldLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,14> english={{"Persist board-energy history","Selection ms [500..60000]","Maximum gap ms [selection..60000]","Flush ms [50..60000]","Buffered readings [1..128]","Batch readings [1..buffer]","Raw batches [1..4096]","Raw readings [1..131072]","Lifetime sessions [1..1024]","Lifetime windows [1..4096]","Database bytes [1..128 MiB]","Exact compaction (new store)","Compact after batches [1..raw]","Exact segments [1..32768]"}};
    for(std::size_t key=0;key<14;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::energyAuthorizationFieldMessageKeyName(k))).second&&i18n::energyAuthorizationFieldMessageKeyName(k).find("energy_authorization.field.")==0&&i18n::energyAuthorizationFieldEnglish(k)==english[key]);CHECK(i18n::energyAuthorizationFieldKeys()[key]==k);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::energyAuthorizationFieldLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<14;++key){const auto message=i18n::energyAuthorizationFieldMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            CHECK(i18n::templateParameters(message.text,&parameters)&&parameters.empty());
            for(const auto *token:{" ms","MiB"})if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);
            const auto begin=english[key].find('['),end=english[key].find(']');if(begin!=english[key].npos)CHECK(end!=english[key].npos&&message.text.find(english[key].substr(begin,end-begin+1))!=message.text.npos);}}
    CHECK(i18n::energyAuthorizationFieldMessageKeyName(Key::Count).empty()&&i18n::energyAuthorizationFieldMessage("en",Key::Count).text.empty()&&i18n::energyAuthorizationFieldEnglish(Key::Count).empty());
}
void matrix(){
    for(const auto &pack:i18n::energyAuthorizationFieldLocalePacks())for(std::size_t key=0;key<14;++key){TerminalEnergyAuthorizationFieldMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<Key>(key);request.literalTone=Tone::Green;
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalEnergyAuthorizationFieldMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size()&&line.metadata.sources[run.content.sourceSpan].empty()&&run.tone==Tone::Green);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalEnergyAuthorizationFieldMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover))CHECK(unisolate(joined)==i18n::energyAuthorizationFieldMessage(recover?"en":pack.id,request.key).text&&!line.line.line.clipped);
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalEnergyAuthorizationFieldMessage(request,policy,128,&rows)&&renderWrappedTerminalEnergyAuthorizationFieldMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<10;++mode){TerminalEnergyAuthorizationFieldMessageRequest bad;TerminalTextPolicy policy;policy.order=order;
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
        CHECK(!layoutTerminalEnergyAuthorizationFieldMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalEnergyAuthorizationFieldMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalEnergyAuthorizationFieldMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalEnergyAuthorizationFieldMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    TerminalEnergyAuthorizationFieldMessageRequest request;request.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalEnergyAuthorizationFieldMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    policy.columns=3;request.key=Key::DatabaseBytes;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalEnergyAuthorizationFieldMessage(request,policy,1,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalEnergyAuthorizationFieldMessage({},policy,nullptr)&&!wrapTerminalEnergyAuthorizationFieldMessage({},policy,1,nullptr)&&!renderTerminalEnergyAuthorizationFieldMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalEnergyAuthorizationFieldMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();matrix();failures();return finishTests("sixteen-pack energy persistence field labels without consent, enrollment or capture-plan authority");}
