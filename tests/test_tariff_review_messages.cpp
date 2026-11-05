// Data-only private change records and independent display envelopes; never parser admission or disclosure/Save authority.
#include "product/terminal_settings_message.h"
#include "product/tariff_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::TariffReviewMessageKey;
const std::array<std::string,4> english={{"{path}: {before} -> {after}",": {before} -> {after}","{path} rows: {before} -> {after}"," rows: {before} -> {after}"}};
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::tariffReviewMessageKeyCount==4&&i18n::tariffReviewLocalePacks().size()==16);std::set<std::string> identities;
    for(std::size_t key=0;key<4;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::tariffReviewMessageKeyName(k))).second&&i18n::tariffReviewMessageKeyName(k).find("tariff.review.")==0&&i18n::tariffReviewMessage("en",k).text==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::tariffReviewLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<4;++key){const auto message=i18n::tariffReviewMessage(pack.id,static_cast<Key>(key));CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text)&&message.text.find(" -> ")!=message.text.npos);}}
    CHECK(i18n::tariffReviewEnglish(Key::Value,{{"after","{path}"},{"before","{after}"},{"path","{before}"}})=="{before}: {after} -> {path}");
    CHECK(i18n::tariffReviewEnglish(Key::RowsRoot,{{"after","32"},{"before","0"}})==" rows: 0 -> 32");
    for(const auto &raw:std::vector<std::string>{"",std::string(5000,'x'),std::string("a\x1b\0b",4),u8"ع","{before}"})CHECK(i18n::tariffReviewEnglish(Key::ValueRoot,{{"before",raw},{"after","null"}})==": "+raw+" -> null");
    CHECK(i18n::tariffReviewEnglish(Key::ValueRoot,{{"before","x"}}).empty()&&i18n::tariffReviewEnglish(Key::ValueRoot,{{"before","x"},{"before","y"}}).empty()&&i18n::tariffReviewEnglish(Key::ValueRoot,{{"before","x"},{"after","y"},{"path",""}}).empty());
    CHECK(i18n::tariffReviewMessageKeyName(Key::Count).empty()&&i18n::tariffReviewMessage("en",Key::Count).text.empty()&&i18n::tariffReviewEnglish(Key::Count).empty());
}
JsonValue json(const char *text){JsonValue value;CHECK(parseJsonStrict(text,&value,nullptr));return value;}
void records(){
    const auto a=json("{\"rows\":[1]}"),b=json("{\"rows\":[2,3]}");const auto changes=tariffDeclarationChangeRecords(a,b);
    CHECK(changes.size()==3&&changes[0].kind==TariffDeclarationChange::Kind::RowCount&&changes[0].path=="rows"&&changes[0].beforeRows==1&&changes[0].afterRows==2&&changes[0].beforeJson.empty()&&changes[0].afterJson.empty());
    CHECK(changes[1].kind==TariffDeclarationChange::Kind::Value&&changes[1].path=="rows[0]"&&changes[1].beforeJson=="1"&&changes[1].afterJson=="2"&&!changes[1].beforeRows&&!changes[1].afterRows);
    CHECK(changes[2].path=="rows[1]"&&changes[2].beforeJson=="null"&&changes[2].afterJson=="3");
    CHECK(tariffDeclarationChanges(a,b)==std::vector<std::string>({"rows rows: 1 -> 2","rows[0]: 1 -> 2","rows[1]: null -> 3"}));
    const auto root=tariffDeclarationChangeRecords({},json("true"));CHECK(root.size()==1&&root[0].path.empty()&&root[0].beforeJson=="null"&&root[0].afterJson=="true");
    CHECK(tariffDeclarationChanges(json("[1]"),json("[2,3]"))[0]==" rows: 1 -> 2");
    std::vector<JsonValue> samples;for(const auto *source:{"null","true","0","1e3","\"{path}{before}\"","[]","[null,true]","{}","{\"\":null}","{\"rows\":[1]}","{\"rows\":[2,3]}"})samples.push_back(json(source));
    for(const auto &before:samples)for(const auto &after:samples){std::vector<std::string> rebuilt;
        for(const auto &record:tariffDeclarationChangeRecords(before,after)){const bool rows=record.kind==TariffDeclarationChange::Kind::RowCount;i18n::MessageArguments arguments;
            if(!record.path.empty())arguments.emplace_back("path",record.path);
            arguments.emplace_back("before",rows?std::to_string(record.beforeRows):record.beforeJson);arguments.emplace_back("after",rows?std::to_string(record.afterRows):record.afterJson);
            rebuilt.push_back(i18n::tariffReviewEnglish(rows?(record.path.empty()?Key::RowsRoot:Key::Rows):(record.path.empty()?Key::ValueRoot:Key::Value),arguments));
            CHECK(rows?(record.beforeJson.empty()&&record.afterJson.empty()):(!record.beforeRows&&!record.afterRows&&!record.beforeJson.empty()&&!record.afterJson.empty()));}
        CHECK(rebuilt==tariffDeclarationChanges(before,after));}
    TariffSettingsPanel panel;panel.open(a);panel.draft=b;const auto active=tariffJson(panel.active),draft=tariffJson(panel.draft);panel.key('v',{},1800000000);
    const auto diff=tariffDeclarationChanges(a,b),complete=tariffDeclarationChanges({},b);std::vector<std::string> actual(panel.review.begin()+8,panel.review.end()-1),expected=diff;expected.insert(expected.end(),complete.begin(),complete.end());
    CHECK(actual==expected&&tariffJson(panel.active)==active&&tariffJson(panel.draft)==draft&&panel.page==TariffSettingsPage::Review&&panel.message.empty());
    CHECK(panel.key(27,{},1800000000)==TariffSettingsResult::None&&panel.page==TariffSettingsPage::Browse&&panel.key(3,{},1800000000)==TariffSettingsResult::Quit);
}
bool rowCounts(Key key){return key==Key::Rows||key==Key::RowsRoot;}
std::size_t maximum(Key key,std::string_view name){return name=="path"?512:rowCounts(key)?256:1024;}
TerminalTariffReviewMessageRequest requestFor(const i18n::TariffReviewLocalePack &pack,Key key){
    TerminalTariffReviewMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;std::vector<std::string_view> names;
    CHECK(i18n::templateParameters(i18n::tariffReviewMessage(pack.id,key).text,&names));
    for(const auto name:names){TerminalSegment value{{name=="path"?"plan.revisions[0].key":name=="before"?"\"{after}\"":"\"{before}\"",{},true},name=="before"?Tone::Canary:Tone::Red};
        if(name!="path"&&rowCounts(key))value.content={name=="before"?"1":"32",{},false,TerminalTextAtom::LeftToRight};
        request.arguments.insert(request.arguments.begin(),{std::string(name),std::move(value)});}
    return request;
}
void matrix(){
    for(const auto &pack:i18n::tariffReviewLocalePacks())for(std::size_t key=0;key<4;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalTariffReviewMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:name=="before"?Tone::Canary:Tone::Red));}}
            for(const auto &arg:request.arguments){std::string actual;for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]==arg.name)actual+=run.content.text;CHECK(actual.empty()||actual==arg.value.content.text);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalTariffReviewMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){const auto text=i18n::tariffReviewMessage(recover?"en":pack.id,request.key).text;std::string expected;std::size_t at=0;
                while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){expected+=text.substr(at);break;}expected+=text.substr(at,start-at);const auto end=text.find('}',start);const auto name=text.substr(start+1,end-start-1);
                    const auto arg=std::find_if(request.arguments.begin(),request.arguments.end(),[&](const auto &a){return a.name==name;});CHECK(arg!=request.arguments.end());expected+=arg->value.content.text;at=end+1;}
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=29;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalTariffReviewMessage(request,policy,128,&rows)&&renderWrappedTerminalTariffReviewMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=29);}
    }
}
void refused(const TerminalTariffReviewMessageRequest &bad,const TerminalTextPolicy &policy){
    TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
    PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
    CHECK(!layoutTerminalTariffReviewMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
    CHECK(!wrapTerminalTariffReviewMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
    CHECK(!renderTerminalTariffReviewMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
    CHECK(!renderWrappedTerminalTariffReviewMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});
}
void failures(){
    for(const auto &pack:i18n::tariffReviewLocalePacks())for(std::size_t key=0;key<4;++key)for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
        const auto original=requestFor(pack,static_cast<Key>(key));TerminalTextPolicy policy;policy.order=order;auto bad=original;
        bad.arguments.push_back({"extra",{{"x",{},true},Tone::Red}});refused(bad,policy);bad=original;bad.arguments.pop_back();refused(bad,policy);bad=original;bad.arguments.push_back(bad.arguments[0]);refused(bad,policy);
        for(std::size_t i=0;i<original.arguments.size();++i)for(unsigned poison=0;poison<8;++poison){bad=original;auto &span=bad.arguments[i].value.content;const auto max=maximum(bad.key,bad.arguments[i].name);
            if(poison==0)span.invariant=!span.invariant;
            if(poison==1)span.text.clear();
            if(poison==2)span.text="\x1b";
            if(poison==3)span.text=std::string(max+1,'x');
            if(poison==4)span.ascii=std::string(max+1,'x');
            if(poison==5){span.text="\xC3";span.ascii="safe";}
            if(poison==6)span.ascii="\x1b";
            if(poison==7){if(span.invariant)span.ascii="substituted";else span.atom=TerminalTextAtom::None;}
            refused(bad,policy);}
    }
    TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=4096;TerminalMessageLine line;auto request=requestFor(i18n::tariffReviewLocalePacks()[0],Key::ValueRoot);
    for(auto &arg:request.arguments){arg.value.content.text=std::string(1024,'x');}CHECK(layoutTerminalTariffReviewMessage(request,policy,&line));policy.columns=19;
    PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalTariffReviewMessage(request,policy,128,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(layoutTerminalTariffReviewMessage(request,policy,&line));for(const auto &run:line.line.runs)if(!run.content.control)CHECK(line.metadata.sources[run.content.sourceSpan].empty());
    policy.columns=1024;request=requestFor(i18n::tariffReviewLocalePacks()[0],Key::RowsRoot);for(auto &arg:request.arguments)if(arg.name=="before")arg.value.content={u8"١","1",false,TerminalTextAtom::LeftToRight};
    CHECK(layoutTerminalTariffReviewMessage(request,policy,&line)&&unisolate(line.line.line.text)==(terminalTextBackend().unicode?u8" rows: ١ -> 32":" rows: 1 -> 32"));
    request=requestFor(i18n::tariffReviewLocalePacks()[0],Key::ValueRoot);request.language="en-US";CHECK(layoutTerminalTariffReviewMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    request.key=Key::Count;refused(request,policy);request.key=Key::ValueRoot;request.language="en@private";refused(request,policy);request.language="en";request.literalTone=static_cast<Tone>(99);refused(request,policy);
    CHECK(!layoutTerminalTariffReviewMessage({},policy,nullptr)&&!wrapTerminalTariffReviewMessage({},policy,1,nullptr)&&!renderTerminalTariffReviewMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalTariffReviewMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();records();matrix();failures();return finishTests("sixteen-pack private tariff change records with original JSON/path and separate human row counts");}
