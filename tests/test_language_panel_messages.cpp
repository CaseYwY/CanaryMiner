// Language shell/review envelopes only. Machine request values remain invariant; draft catalogs stay inactive.
#include "product/terminal_settings_message.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::LanguagePanelMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
std::vector<std::string> keys(std::string_view text){std::vector<std::string> result;std::size_t at=0;
    while((at=text.find('[',at))!=text.npos){const auto end=text.find(']',at);CHECK(end!=text.npos);if(end==text.npos)break;result.emplace_back(text.substr(at,end-at+1));at=end+1;}return result;}
void catalogs(){
    CHECK(i18n::languagePanelMessageKeyCount==32&&i18n::languagePanelLocalePacks().size()==16);std::set<std::string> identities;const auto &source=i18n::languagePanelLocalePacks()[0];
    for(std::size_t key=0;key<32;++key){const auto k=static_cast<Key>(key);const auto identity=i18n::languagePanelMessageKeyName(k);CHECK(identity.find("language.panel.")==0&&identities.insert(std::string(identity)).second);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::languagePanelLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<32;++key){const auto message=i18n::languagePanelMessage(pack.id,static_cast<Key>(key));
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(source.messages[key],message.text));
            CHECK(keys(source.messages[key])==keys(message.text));for(const auto *token:{"language-preferences.json","CLI","ASCII","13","12","en"}){
                const bool protect=std::string_view(token)!="en"||key==24||key==28;
                if(protect&&source.messages[key].find(token)!=source.messages[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}}
    CHECK(i18n::languagePanelEnglish(Key::Title)=="Language / format requests"&&i18n::languagePanelEnglish(Key::EndMarker)=="[End language]");
    CHECK(i18n::languagePanelEnglish(Key::Before,{{"value","ar"}})=="Before: ar"&&i18n::languagePanelEnglish(Key::After,{{"value","locale_default"}})=="After: locale_default");
    CHECK(i18n::languagePanelEnglish(Key::FieldValue,{{"value","rtl"},{"field","Direction"}})=="Direction: rtl");
    CHECK(i18n::languagePanelEnglish(Key::FieldValue,{{"field","{value}"},{"value","{field}"}})=="{value}: {field}");
    CHECK(i18n::languagePanelEnglish(Key::Before).empty()&&i18n::languagePanelEnglish(Key::Title,{{"value","ar"}}).empty());
    CHECK(i18n::languagePanelEnglish(Key::FieldValue,{{"field","x"},{"field","y"}}).empty()&&i18n::languagePanelEnglish(Key::Before,{{"unexpected","ar"}}).empty());
    for(const auto &bad:std::vector<std::string>{u8"ع","\x1b",std::string(4097,'x')})CHECK(i18n::languagePanelEnglish(Key::Before,{{"value",bad}}).empty());
    CHECK(i18n::languagePanelEnglish(Key::Before,{{"value",std::string(4088,'x')}}).size()==4096&&i18n::languagePanelEnglish(Key::Before,{{"value",std::string(4089,'x')}}).empty());
    CHECK(i18n::languagePanelMessageKeyName(Key::Count).empty()&&i18n::languagePanelMessage("en",Key::Count).text.empty()&&i18n::languagePanelEnglish(Key::Count).empty());
}
TerminalLanguagePanelMessageRequest requestFor(const i18n::LanguagePanelLocalePack &pack,Key key){
    TerminalLanguagePanelMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;std::vector<std::string_view> parameters;
    CHECK(i18n::templateParameters(i18n::languagePanelMessage(pack.id,key).text,&parameters));
    for(const auto name:parameters){TerminalSegment value{{"locale_default",{},true},Tone::Red};
        if(name=="field")value={{std::string(i18n::languageFieldMessage(pack.id,i18n::LanguageFieldMessageKey::FormatLocale).text),"Format locale"},Tone::Canary};
        if(name=="review")value={{std::string(i18n::languageFieldMessage(pack.id,i18n::LanguageFieldMessageKey::DraftReview).text),"machine-assisted draft (not human reviewed)"},Tone::Canary};
        if(name=="format")value={{std::string(i18n::languagePanelMessage(pack.id,Key::LegacyNumberFormat).text),"legacy per-field formatting"},Tone::Canary};
        request.arguments.insert(request.arguments.begin(),{std::string(name),std::move(value)});}
    return request;
}
void matrix(){
    for(const auto &pack:i18n::languagePanelLocalePacks())for(std::size_t key=0;key<32;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalLanguagePanelMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined,machine;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:name=="value"?Tone::Red:Tone::Canary));if(name=="value")machine+=run.content.text;}}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width&&(machine.empty()||machine=="locale_default"));
            PaintedTerminalMessageLine painted;CHECK(renderTerminalLanguagePanelMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){const auto text=i18n::languagePanelMessage(recover?"en":pack.id,request.key).text;std::string expected;std::size_t at=0;
                while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){expected+=text.substr(at);break;}expected+=text.substr(at,start-at);const auto end=text.find('}',start);const auto name=text.substr(start+1,end-start-1);
                    const auto arg=std::find_if(request.arguments.begin(),request.arguments.end(),[&](const auto &a){return a.name==name;});CHECK(arg!=request.arguments.end());const auto &span=arg->value.content;expected+=recover?(span.ascii.empty()?span.text:span.ascii):span.text;at=end+1;}
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalLanguagePanelMessage(request,policy,128,&rows)&&renderWrappedTerminalLanguagePanelMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    const auto request=requestFor(i18n::languagePanelLocalePacks()[0],Key::Before);
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<14;++mode){auto bad=request;TerminalTextPolicy policy;policy.order=order;
        if(mode==0)bad.key=Key::Count;
        if(mode==1)bad.arguments.clear();
        if(mode==2)bad.arguments.push_back(bad.arguments[0]);
        if(mode==3)bad.arguments[0].name="unexpected";
        if(mode==4)bad.arguments[0].value.content.invariant=false;
        if(mode==5)bad.arguments[0].value.content.text=u8"١";
        if(mode==6)bad.arguments[0].value.content.ascii="substituted";
        if(mode==7)bad.arguments[0].value.content.text="\x1b";
        if(mode==8)bad.language="en@private";
        if(mode==9)bad.literalTone=static_cast<Tone>(99);
        if(mode==10)bad.arguments[0].value.content.text=std::string(4097,'x');
        if(mode==11)bad.arguments[0].value.content.ascii=std::string(4097,'x');
        if(mode==12)bad.arguments[0].name=std::string(33,'x');
        if(mode==13)bad.key=Key::Title;
        TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
        PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
        CHECK(!layoutTerminalLanguagePanelMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalLanguagePanelMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalLanguagePanelMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalLanguagePanelMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    auto unknown=request;unknown.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalLanguagePanelMessage(unknown,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    policy.columns=3;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalLanguagePanelMessage(request,policy,128,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalLanguagePanelMessage({},policy,nullptr)&&!wrapTerminalLanguagePanelMessage({},policy,1,nullptr)&&!renderTerminalLanguagePanelMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalLanguagePanelMessage({},policy,1,ColorLevel::None,nullptr));
}
}
int main(){catalogs();matrix();failures();return finishTests("sixteen-pack language shell with invariant machine requests and explicit inactive-state notices");}
