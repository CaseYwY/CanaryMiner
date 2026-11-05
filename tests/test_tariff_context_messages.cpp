// Offline contextual envelopes only; synthetic schema paths and counts are not validation, identities or Save receipts.
#include "product/terminal_settings_message.h"
#include "product/tariff_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::TariffContextMessageKey;
const std::array<std::string,10> english={{"Edit {field}","Edit row {position}","Edit ","> {input}_","Remove draft row {position}?","row {position}","[Enter] {count} rows","Path: {path}","Path: declaration","Field {position}/{count}"}};
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::tariffContextMessageKeyCount==10&&i18n::tariffContextLocalePacks().size()==16);std::set<std::string> identities;
    for(std::size_t key=0;key<10;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::tariffContextMessageKeyName(k))).second&&i18n::tariffContextMessageKeyName(k).find("tariff.context.")==0&&i18n::tariffContextMessage("en",k).text==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::tariffContextLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<10;++key){const auto message=i18n::tariffContextMessage(pack.id,static_cast<Key>(key));CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            if(key==6)CHECK(message.text.find("[Enter]")!=message.text.npos);
            if(key==3)CHECK(message.text.substr(0,2)=="> "&&message.text.back()=='_');}}
    CHECK(i18n::tariffContextEnglish(Key::FieldPosition,{{"count","{position}"},{"position","{count}"}})=="Field {count}/{position}");
    CHECK(i18n::tariffContextEnglish(Key::EditInput,{{"input",""}})=="> _"&&i18n::tariffContextEnglish(Key::EditBlank)=="Edit "&&i18n::tariffContextEnglish(Key::RootPath)=="Path: declaration");
    for(const auto &raw:std::vector<std::string>{"",std::string(5000,'x'),std::string("a\x1b\0b",4),u8"ع", "{input}"})CHECK(i18n::tariffContextEnglish(Key::EditInput,{{"input",raw}})=="> "+raw+"_"); // source-only preservation, not typed admission
    CHECK(i18n::tariffContextEnglish(Key::FieldPosition,{{"position","1"}}).empty()&&i18n::tariffContextEnglish(Key::FieldPosition,{{"position","1"},{"position","2"}}).empty()&&i18n::tariffContextEnglish(Key::RootPath,{{"extra","x"}}).empty());
    CHECK(i18n::tariffContextMessageKeyName(Key::Count).empty()&&i18n::tariffContextMessage("en",Key::Count).text.empty()&&i18n::tariffContextEnglish(Key::Count).empty());
}
void native(){
    JsonValue seed,array,object;seed.type=object.type=JsonValue::Type::Object;array.type=JsonValue::Type::Array;array.items={object,object};seed.members.emplace_back("synthetic_rows",array);
    TariffSettingsPanel panel;panel.open(seed);const auto old=tariffJson(seed);const auto rendered=[&]{return panel.render(1024,1000,ColorLevel::None);};
    CHECK(rendered().find("synthetic_rows: [Enter] 2 rows\n")!=std::string::npos&&rendered().find("Path: declaration\n")!=std::string::npos&&rendered().find("Field 1/1\n")!=std::string::npos);
    panel.key('\r',{},1800000000);CHECK(rendered().find("row 0: [Enter] fields\n")!=std::string::npos&&rendered().find("Path: synthetic_rows\n")!=std::string::npos&&rendered().find("Field 1/2\n")!=std::string::npos);
    panel.key(kKeyDown,{},1800000000);panel.key('x',{},1800000000);CHECK(rendered().find("Remove draft row 1?\n")!=std::string::npos&&tariffJson(panel.draft)==old&&tariffJson(panel.active)==old);
    panel.key(27,{},1800000000);panel.key(27,{},1800000000);CHECK(!panel.dirty()&&panel.route.empty());
    JsonValue value;value.type=JsonValue::Type::String;value.text="synthetic-{input}";seed.members={{"{field}",value}};panel.open(seed);panel.key('\r',{},1800000000);
    CHECK(rendered().find("Edit {field}\n")!=std::string::npos);
    for(const auto &input:std::vector<std::string>{"","qDLS","{input}",std::string(128,'x'),std::string(129,'x')}){panel.input=input;CHECK(rendered().find("> "+input+"_\n")!=std::string::npos&&tariffJson(panel.active)==tariffJson(seed)&&!panel.dirty());}
    array.items={value};panel.open(array);panel.key('\r',{},1800000000);CHECK(rendered().find("Edit row 0\n")!=std::string::npos&&!panel.dirty());
    array.items.clear();panel.open(array);panel.page=TariffSettingsPage::Edit;CHECK(rendered().find("Edit \n")!=std::string::npos);panel.page=TariffSettingsPage::Browse;CHECK(rendered().find("Field 0/0\n")!=std::string::npos);
    CHECK(panel.key(3,{},1800000000)==TariffSettingsResult::Quit);
}
std::size_t maximum(std::string_view name){return name=="field"||name=="input"?128:name=="path"?512:256;}
TerminalTariffContextMessageRequest requestFor(const i18n::TariffContextLocalePack &pack,Key key){
    TerminalTariffContextMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;std::vector<std::string_view> names;
    CHECK(i18n::templateParameters(i18n::tariffContextMessage(pack.id,key).text,&names));
    for(const auto name:names){TerminalSegment value{{name=="field"?"source_reference":name=="path"?"plan.revisions[0].key":"{input}",{},true},Tone::Red};
        if(name=="position"||name=="count")value={{name=="position"?"1":"32",{},false,TerminalTextAtom::LeftToRight},Tone::Canary};
        request.arguments.insert(request.arguments.begin(),{std::string(name),std::move(value)});}
    return request;
}
void matrix(){
    for(const auto &pack:i18n::tariffContextLocalePacks())for(std::size_t key=0;key<10;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalTariffContextMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:name=="position"||name=="count"?Tone::Canary:Tone::Red));}}
            for(const auto &arg:request.arguments){std::string actual;for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]==arg.name)actual+=run.content.text;CHECK(actual.empty()||actual==arg.value.content.text);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalTariffContextMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){const auto text=i18n::tariffContextMessage(recover?"en":pack.id,request.key).text;std::string expected;std::size_t at=0;
                while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){expected+=text.substr(at);break;}expected+=text.substr(at,start-at);const auto end=text.find('}',start);const auto name=text.substr(start+1,end-start-1);
                    const auto arg=std::find_if(request.arguments.begin(),request.arguments.end(),[&](const auto &a){return a.name==name;});CHECK(arg!=request.arguments.end());expected+=arg->value.content.text;at=end+1;}
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=29;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalTariffContextMessage(request,policy,128,&rows)&&renderWrappedTerminalTariffContextMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=29);}
    }
}
void refused(const TerminalTariffContextMessageRequest &bad,const TerminalTextPolicy &policy){
    TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
    PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
    CHECK(!layoutTerminalTariffContextMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
    CHECK(!wrapTerminalTariffContextMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
    CHECK(!renderTerminalTariffContextMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
    CHECK(!renderWrappedTerminalTariffContextMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});
}
void failures(){
    for(const auto &pack:i18n::tariffContextLocalePacks())for(std::size_t key=0;key<10;++key)for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
        const auto original=requestFor(pack,static_cast<Key>(key));TerminalTextPolicy policy;policy.order=order;auto bad=original;
        bad.arguments.push_back({"extra",{{"x",{},true},Tone::Red}});refused(bad,policy);
        if(!original.arguments.empty()){bad=original;bad.arguments.pop_back();refused(bad,policy);bad=original;bad.arguments.push_back(bad.arguments[0]);refused(bad,policy);}
        for(std::size_t i=0;i<original.arguments.size();++i)for(unsigned poison=0;poison<8;++poison){bad=original;auto &span=bad.arguments[i].value.content;const auto max=maximum(bad.arguments[i].name);
            if(poison==0)span.invariant=!span.invariant;
            if(poison==1){if(bad.arguments[i].name=="input")span.text=u8"١";else span.text.clear();}
            if(poison==2)span.text="\x1b";
            if(poison==3)span.text=std::string(max+1,'x');
            if(poison==4)span.ascii=std::string(max+1,'x');
            if(poison==5){span.text="\xC3";span.ascii="safe";}
            if(poison==6)span.ascii="\x1b";
            if(poison==7){if(span.invariant)span.ascii="substituted";else span.atom=TerminalTextAtom::None;}
            refused(bad,policy);}
    }
    TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=1024;TerminalMessageLine line;auto request=requestFor(i18n::tariffContextLocalePacks()[0],Key::EditInput);
    for(const auto &input:std::vector<std::string>{"","{input}",std::string(128,'x')}){request.arguments[0].value.content.text=input;CHECK(layoutTerminalTariffContextMessage(request,policy,&line)&&unisolate(line.line.line.text)=="> "+input+"_");}
    request=requestFor(i18n::tariffContextLocalePacks()[0],Key::FieldPosition);for(auto &arg:request.arguments)if(arg.name=="position")arg.value.content={u8"١","1",false,TerminalTextAtom::LeftToRight};
    CHECK(layoutTerminalTariffContextMessage(request,policy,&line)&&unisolate(line.line.line.text)==(terminalTextBackend().unicode?u8"Field ١/32":"Field 1/32"));
    request=requestFor(i18n::tariffContextLocalePacks()[0],Key::Path);request.arguments[0].value.content.text=std::string(512,'x');CHECK(layoutTerminalTariffContextMessage(request,policy,&line));policy.columns=19;
    PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalTariffContextMessage(request,policy,128,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(layoutTerminalTariffContextMessage(request,policy,&line));for(const auto &run:line.line.runs)if(!run.content.control)CHECK(line.metadata.sources[run.content.sourceSpan]!="path");
    policy.columns=1024;request=requestFor(i18n::tariffContextLocalePacks()[0],Key::RootPath);request.language="en-US";CHECK(layoutTerminalTariffContextMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    request.key=Key::Count;refused(request,policy);request.key=Key::RootPath;request.language="en@private";refused(request,policy);request.language="en";request.literalTone=static_cast<Tone>(99);refused(request,policy);
    CHECK(!layoutTerminalTariffContextMessage({},policy,nullptr)&&!wrapTerminalTariffContextMessage({},policy,1,nullptr)&&!renderTerminalTariffContextMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalTariffContextMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack offline tariff editor/path/count envelopes with separate machine originals and human atomic positions");}
