// Whole offline browse rows only; raw tokens and synthetic contexts are not JSON/model admission or disclosure authority.
#include "product/terminal_settings_message.h"
#include "product/tariff_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::TariffValueMessageKey;
const std::array<std::string,7> english={{"{field}: {value}","{field}: {value} [fixed]","{field}: [Enter] fields","{field}: [Enter] {count} rows","row {position}: {value}","row {position}: [Enter] fields","row {position}: [Enter] {count} rows"}};
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::tariffValueMessageKeyCount==7&&i18n::tariffValueLocalePacks().size()==16);std::set<std::string> identities;
    for(std::size_t key=0;key<7;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::tariffValueMessageKeyName(k))).second&&i18n::tariffValueMessageKeyName(k).find("tariff.value.")==0&&i18n::tariffValueMessage("en",k).text==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::tariffValueLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<7;++key){const auto message=i18n::tariffValueMessage(pack.id,static_cast<Key>(key));CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            if(english[key].find("[Enter]")!=std::string::npos)CHECK(message.text.find("[Enter]")!=message.text.npos);
            if(key==1)CHECK(message.text.find(" [")!=message.text.npos&&message.text.back()==']');}}
    CHECK(i18n::tariffValueEnglish(Key::FieldJson,{{"value","{field}"},{"field","{value}"}})=="{value}: {field}");
    CHECK(i18n::tariffValueEnglish(Key::RowRows,{{"count","32"},{"position","0"}})=="row 0: [Enter] 32 rows");
    for(const auto &raw:std::vector<std::string>{"",std::string(5000,'x'),std::string("a\x1b\0b",4),u8"ع","{value}"})CHECK(i18n::tariffValueEnglish(Key::FieldJson,{{"field","x"},{"value",raw}})=="x: "+raw); // source-only, not typed admission
    CHECK(i18n::tariffValueEnglish(Key::FieldJson,{{"field","x"}}).empty()&&i18n::tariffValueEnglish(Key::FieldJson,{{"field","x"},{"field","y"}}).empty()&&i18n::tariffValueEnglish(Key::FieldFields,{{"field","x"},{"extra","x"}}).empty());
    CHECK(i18n::tariffValueMessageKeyName(Key::Count).empty()&&i18n::tariffValueMessage("en",Key::Count).text.empty()&&i18n::tariffValueEnglish(Key::Count).empty());
}
void native(){
    for(const auto *encoded:{"null","true","false","0","-0","1.5","1e3","\"\"","\"{field}{value}\"","\"q\\\"\\\\\\n\\u0000D\""}){JsonValue value;CHECK(parseJsonStrict(encoded,&value,nullptr));const auto raw=tariffJson(value);
        for(const auto *field:{"country","schema_version","confirmed_utc","reset_policy","rounding","basis","origin","time_database_version","{value}"}){const bool fixed=std::string(field)!="country"&&std::string(field)!="{value}";
            JsonValue seed;seed.type=JsonValue::Type::Object;seed.members.emplace_back(field,value);const auto before=tariffJson(seed);TariffSettingsPanel panel;panel.open(seed);
            const auto text=panel.render(1024,1000,ColorLevel::None);CHECK(text.find(std::string("> ")+field+": "+raw+(fixed?" [fixed]":"")+"\n")!=text.npos&&tariffJson(panel.active)==before&&tariffJson(panel.draft)==before&&!panel.dirty());}
        JsonValue array;array.type=JsonValue::Type::Array;array.items={value};TariffSettingsPanel panel;panel.open(array);CHECK(panel.render(1024,1000,ColorLevel::None).find("> row 0: "+raw+"\n")!=std::string::npos&&!panel.dirty());
    }
    JsonValue object,array;object.type=JsonValue::Type::Object;array.type=JsonValue::Type::Array;array.items.resize(2);
    for(const auto &value:{object,array}){const auto suffix=value.isObject()?"[Enter] fields":"[Enter] 2 rows";JsonValue root;root.type=JsonValue::Type::Object;root.members.emplace_back("synthetic_field",value);TariffSettingsPanel panel;panel.open(root);
        CHECK(panel.render(1024,1000,ColorLevel::None).find(std::string("> synthetic_field: ")+suffix+"\n")!=std::string::npos&&!panel.dirty());
        root.type=JsonValue::Type::Array;root.members.clear();root.items={value};panel.open(root);CHECK(panel.render(1024,1000,ColorLevel::None).find(std::string("> row 0: ")+suffix+"\n")!=std::string::npos&&!panel.dirty());
        CHECK(panel.key(3,{},1800000000)==TariffSettingsResult::Quit);}
}
std::size_t maximum(std::string_view name){return name=="field"?128:name=="value"?1024:256;}
TerminalTariffValueMessageRequest requestFor(const i18n::TariffValueLocalePack &pack,Key key){
    TerminalTariffValueMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;std::vector<std::string_view> names;
    CHECK(i18n::templateParameters(i18n::tariffValueMessage(pack.id,key).text,&names));
    for(const auto name:names){TerminalSegment value{{name=="field"?"source_reference":"\"{value}\"",{},true},Tone::Red};
        if(name=="position"||name=="count")value={{name=="position"?"0":"32",{},false,TerminalTextAtom::LeftToRight},Tone::Canary};
        request.arguments.insert(request.arguments.begin(),{std::string(name),std::move(value)});}
    return request;
}
void matrix(){
    for(const auto &pack:i18n::tariffValueLocalePacks())for(std::size_t key=0;key<7;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalTariffValueMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:name=="position"||name=="count"?Tone::Canary:Tone::Red));}}
            for(const auto &arg:request.arguments){std::string actual;for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]==arg.name)actual+=run.content.text;CHECK(actual.empty()||actual==arg.value.content.text);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalTariffValueMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){const auto text=i18n::tariffValueMessage(recover?"en":pack.id,request.key).text;std::string expected;std::size_t at=0;
                while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){expected+=text.substr(at);break;}expected+=text.substr(at,start-at);const auto end=text.find('}',start);const auto name=text.substr(start+1,end-start-1);
                    const auto arg=std::find_if(request.arguments.begin(),request.arguments.end(),[&](const auto &a){return a.name==name;});CHECK(arg!=request.arguments.end());expected+=arg->value.content.text;at=end+1;}
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=29;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalTariffValueMessage(request,policy,128,&rows)&&renderWrappedTerminalTariffValueMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=29);}
    }
}
void refused(const TerminalTariffValueMessageRequest &bad,const TerminalTextPolicy &policy){
    TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
    PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
    CHECK(!layoutTerminalTariffValueMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
    CHECK(!wrapTerminalTariffValueMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
    CHECK(!renderTerminalTariffValueMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
    CHECK(!renderWrappedTerminalTariffValueMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});
}
void failures(){
    for(const auto &pack:i18n::tariffValueLocalePacks())for(std::size_t key=0;key<7;++key)for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
        const auto original=requestFor(pack,static_cast<Key>(key));TerminalTextPolicy policy;policy.order=order;auto bad=original;
        bad.arguments.push_back({"extra",{{"x",{},true},Tone::Red}});refused(bad,policy);bad=original;bad.arguments.pop_back();refused(bad,policy);bad=original;bad.arguments.push_back(bad.arguments[0]);refused(bad,policy);
        for(std::size_t i=0;i<original.arguments.size();++i)for(unsigned poison=0;poison<8;++poison){bad=original;auto &span=bad.arguments[i].value.content;const auto max=maximum(bad.arguments[i].name);
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
    TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=4096;TerminalMessageLine line;auto request=requestFor(i18n::tariffValueLocalePacks()[0],Key::FieldJson);
    for(auto &arg:request.arguments){if(arg.name=="value")arg.value.content.text=std::string(1024,'x');}CHECK(layoutTerminalTariffValueMessage(request,policy,&line));policy.columns=19;
    PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalTariffValueMessage(request,policy,128,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(layoutTerminalTariffValueMessage(request,policy,&line));for(const auto &run:line.line.runs)if(!run.content.control)CHECK(line.metadata.sources[run.content.sourceSpan]!="value");
    policy.columns=1024;request=requestFor(i18n::tariffValueLocalePacks()[0],Key::RowRows);for(auto &arg:request.arguments)if(arg.name=="position")arg.value.content={u8"٠","0",false,TerminalTextAtom::LeftToRight};
    CHECK(layoutTerminalTariffValueMessage(request,policy,&line)&&unisolate(line.line.line.text)==(terminalTextBackend().unicode?u8"row ٠: [Enter] 32 rows":"row 0: [Enter] 32 rows"));
    request=requestFor(i18n::tariffValueLocalePacks()[0],Key::FieldJson);for(auto &arg:request.arguments)if(arg.name=="value")arg.value.content.text="not-json";request.language="en-US";
    CHECK(layoutTerminalTariffValueMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en"); // opaque bytes, not JSON admission
    request.key=Key::Count;refused(request,policy);request.key=Key::FieldJson;request.language="en@private";refused(request,policy);request.language="en";request.literalTone=static_cast<Tone>(99);refused(request,policy);
    CHECK(!layoutTerminalTariffValueMessage({},policy,nullptr)&&!wrapTerminalTariffValueMessage({},policy,1,nullptr)&&!renderTerminalTariffValueMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalTariffValueMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack whole offline tariff browse rows with original JSON and independent human atomic positions");}
