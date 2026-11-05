// Runtime shell/editor text only: contextual input is opaque, not a number parser or live action.
#include "product/terminal_settings_message.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::RuntimePanelMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
std::vector<std::string> keys(std::string_view text){std::vector<std::string> result;std::size_t at=0;
    while((at=text.find('[',at))!=text.npos){const auto end=text.find(']',at);CHECK(end!=text.npos);if(end==text.npos)break;result.emplace_back(text.substr(at,end-at+1));at=end+1;}return result;}
void catalogs(){
    CHECK(i18n::runtimePanelMessageKeyCount==30&&i18n::runtimePanelLocalePacks().size()==16);std::set<std::string> identities;const auto &source=i18n::runtimePanelLocalePacks()[0];
    for(std::size_t key=0;key<30;++key){const auto k=static_cast<Key>(key);const auto identity=i18n::runtimePanelMessageKeyName(k);CHECK(identity.find("runtime.panel.")==0&&identities.insert(std::string(identity)).second);
        CHECK(key==10?i18n::runtimePanelEnglish(k).empty():i18n::runtimePanelEnglish(k)==source.messages[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::runtimePanelLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<30;++key){const auto message=i18n::runtimePanelMessage(pack.id,static_cast<Key>(key));
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(source.messages[key],message.text));
            CHECK(keys(source.messages[key])==keys(message.text));
            for(const auto *token:{"CanaryMiner","runtime-preferences.json","CLI","Q/D/L/S","Ctrl+C","Esc","1000","12","GPU"})if(source.messages[key].find(token)!=source.messages[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    for(const auto &input:std::vector<std::string>{"","0","1000","qDLS","{input}",std::string(16,'x')})CHECK(i18n::runtimePanelEnglishInput(input)=="> "+input+"_");
    for(const auto &input:std::vector<std::string>{u8"١","\x1b","x\n",std::string(17,'x')})CHECK(i18n::runtimePanelEnglishInput(input).empty());
    CHECK(i18n::runtimePanelEnglish(Key::EditKeys)=="Q/D/L/S are input, not shortcuts. Ctrl+C still stops.");
    CHECK(i18n::runtimePanelMessageKeyName(Key::Count).empty()&&i18n::runtimePanelMessage("en",Key::Count).text.empty()&&i18n::runtimePanelEnglish(Key::Count).empty());
}
TerminalRuntimePanelMessageRequest requestFor(const i18n::RuntimePanelLocalePack &pack,Key key,const std::string &input="qDLS"){
    TerminalRuntimePanelMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;
    if(key==Key::EditInput)request.arguments={{"input",{{input,{},true},Tone::Canary}}};
    return request;
}
void matrix(){
    for(const auto &pack:i18n::runtimePanelLocalePacks())for(std::size_t key=0;key<30;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalRuntimePanelMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined,input;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:Tone::Canary));if(!name.empty())input+=run.content.text;}}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width&&(input.empty()||input=="qDLS"));
            PaintedTerminalMessageLine painted;CHECK(renderTerminalRuntimePanelMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){auto expected=std::string(i18n::runtimePanelMessage(recover?"en":pack.id,request.key).text);
                if(!request.arguments.empty())expected.replace(expected.find("{input}"),7,"qDLS");
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalRuntimePanelMessage(request,policy,128,&rows)&&renderWrappedTerminalRuntimePanelMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    const auto request=requestFor(i18n::runtimePanelLocalePacks()[0],Key::EditInput);
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<12;++mode){auto bad=request;TerminalTextPolicy policy;policy.order=order;
        if(mode==0)bad.key=Key::Count;
        if(mode==1)bad.arguments.clear();
        if(mode==2)bad.arguments.push_back(bad.arguments[0]);
        if(mode==3)bad.arguments[0].name="unexpected";
        if(mode==4)bad.arguments[0].value.content.invariant=false;
        if(mode==5)bad.arguments[0].value.content.text=u8"١";
        if(mode==6)bad.arguments[0].value.content.text=std::string(17,'x');
        if(mode==7)bad.arguments[0].value.content.text="\x1b";
        if(mode==8)bad.arguments[0].value.content.ascii="substituted";
        if(mode==9)bad.language="en@private";
        if(mode==10)bad.literalTone=static_cast<Tone>(99);
        if(mode==11)bad.key=Key::TitleFull;
        TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
        PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
        CHECK(!layoutTerminalRuntimePanelMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalRuntimePanelMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalRuntimePanelMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalRuntimePanelMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    auto unknown=request;unknown.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=1024;TerminalMessageLine line;
    CHECK(layoutTerminalRuntimePanelMessage(unknown,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    for(const auto &input:std::vector<std::string>{"","0","1000","{input}",std::string(16,'x')}){const auto sample=requestFor(i18n::runtimePanelLocalePacks()[0],Key::EditInput,input);
        CHECK(layoutTerminalRuntimePanelMessage(sample,policy,&line)&&unisolate(line.line.line.text)=="> "+input+"_");}
    policy.columns=3;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalRuntimePanelMessage(request,policy,128,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalRuntimePanelMessage({},policy,nullptr)&&!wrapTerminalRuntimePanelMessage({},policy,1,nullptr)&&!renderTerminalRuntimePanelMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalRuntimePanelMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalogs();matrix();failures();return finishTests("sixteen-pack runtime shell with invariant contextual input and unchanged next-start boundaries");}
