// Complete before/after rule display only; literal missing labels are not invented endpoint or consent facts.
#include "product/terminal_settings_message.h"
#include "product/pool_security_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::PoolRuleMessageKey;
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
TerminalPoolRuleMessageRequest requestFor(const i18n::PoolRuleLocalePack &pack,Key key){
    TerminalPoolRuleMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;
    std::vector<std::string_view> names;CHECK(i18n::templateParameters(i18n::poolRuleMessage(pack.id,key).text,&names));
    for(const auto name:names){TerminalSegment value{{name=="mode"?"prefer_tls":name=="consent"?"YES":name=="tls"?"tls.fixture":"tcp.fixture",{},true},Tone::Red};
        request.arguments.insert(request.arguments.begin(),{std::string(name),std::move(value)});}
    return request;
}
void catalog(){
    CHECK(i18n::poolRuleMessageKeyCount==8&&i18n::poolRuleLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,8> english={{"Before: {mode} | TLS: {tls} | TCP: {tcp} | plaintext consent: {consent}","Before: {mode} | TLS: {tls} | TCP: unset | plaintext consent: {consent}","Before: {mode} | TLS: unset | TCP: {tcp} | plaintext consent: {consent}","Before: {mode} | TLS: unset | TCP: unset | plaintext consent: {consent}","After: {mode} | TLS: {tls} | TCP: {tcp} | plaintext consent: {consent}","After: {mode} | TLS: {tls} | TCP: unset | plaintext consent: {consent}","After: {mode} | TLS: unset | TCP: {tcp} | plaintext consent: {consent}","After: {mode} | TLS: unset | TCP: unset | plaintext consent: {consent}"}};
    for(std::size_t key=0;key<8;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::poolRuleMessageKeyName(k))).second&&i18n::poolRuleMessageKeyName(k).find("pool.rule.")==0&&i18n::poolRuleMessage("en",k).text==english[key]);
        const auto sample=requestFor(i18n::poolRuleLocalePacks()[0],k);i18n::MessageArguments args;for(const auto &arg:sample.arguments)args.emplace_back(arg.name,arg.value.content.text);
        const auto variant=key%4;const auto expected=std::string(key<4?"Before: ":"After: ")+"prefer_tls | TLS: "+(variant==0||variant==1?"tls.fixture":"unset")+" | TCP: "+(variant==0||variant==2?"tcp.fixture":"unset")+" | plaintext consent: YES";
        CHECK(i18n::poolRuleEnglish(k,args)==expected);
    }
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::poolRuleLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<8;++key){const auto message=i18n::poolRuleMessage(pack.id,static_cast<Key>(key));
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            CHECK(message.text.find("TLS")!=message.text.npos&&message.text.find("TCP")!=message.text.npos);}}
    CHECK(i18n::poolRuleEnglish(Key::BeforeUnset,{{"mode","{consent}"},{"consent","{mode}"}})=="Before: {consent} | TLS: unset | TCP: unset | plaintext consent: {mode}");
    for(const auto &input:std::vector<std::string>{"",u8"ع","\x1b",std::string(301,'x')})CHECK(i18n::poolRuleEnglish(Key::BeforeUnset,{{"mode",input},{"consent","off"}}).empty());
    CHECK(!i18n::poolRuleEnglish(Key::BeforeUnset,{{"mode",std::string(300,'x')},{"consent","off"}}).empty());
    CHECK(i18n::poolRuleEnglish(Key::BeforeUnset,{{"mode","x"}}).empty()&&i18n::poolRuleEnglish(Key::BeforeUnset,{{"mode","x"},{"mode","y"}}).empty()&&i18n::poolRuleEnglish(Key::BeforeUnset,{{"mode","x"},{"consent","off"},{"tls","unused"}}).empty());
    CHECK(i18n::poolRuleMessageKeyName(Key::Count).empty()&&i18n::poolRuleMessage("en",Key::Count).text.empty()&&i18n::poolRuleEnglish(Key::Count,{}).empty());
}
void native(){
    for(auto mode:{PoolSecurityMode::Unchanged,PoolSecurityMode::StrictTls,PoolSecurityMode::PreferTls})for(unsigned presence=0;presence<4;++presence)for(bool consent:{false,true}){
        PoolSecurityRule rule{mode,presence&1?"stratum+ssl://a:1":"",presence&2?"stratum+tcp://b:2":"",consent};PoolSecurityPreferences preferences;preferences.users={rule};preferences.donation=rule;
        PoolSecuritySettingsPanel panel;panel.open(preferences);panel.page=PoolSecuritySettingsPage::Review;const auto original=poolSecurityPreferencesJson(preferences);const auto frame=panel.render(1024,1000,ColorLevel::None);
        // Rendering deliberately includes inconsistent developer-only combinations: it is not validation or admission.
        const auto summary=std::string(poolSecurityModeName(mode))+" | TLS: "+(rule.tlsUrl.empty()?"unset":rule.tlsUrl)+" | TCP: "+(rule.plaintextUrl.empty()?"unset":rule.plaintextUrl)+" | plaintext consent: "+(consent?"YES":"off");
        CHECK(frame.find("Before: "+summary)!=frame.npos&&frame.find("After: "+summary)!=frame.npos);
        CHECK(poolSecurityPreferencesJson(panel.active)==original&&poolSecurityPreferencesJson(panel.draft)==original&&!panel.dirty());
    }
}
void matrix(){
    for(const auto &pack:i18n::poolRuleLocalePacks())for(std::size_t key=0;key<8;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalPoolRuleMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:Tone::Red));}}
            for(const auto &arg:request.arguments){std::string actual;for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]==arg.name)actual+=run.content.text;CHECK(actual.empty()||actual==arg.value.content.text);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalPoolRuleMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){const auto text=i18n::poolRuleMessage(recover?"en":pack.id,request.key).text;std::string expected;std::size_t at=0;
                while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){expected+=text.substr(at);break;}expected+=text.substr(at,start-at);const auto end=text.find('}',start);const auto name=text.substr(start+1,end-start-1);
                    const auto arg=std::find_if(request.arguments.begin(),request.arguments.end(),[&](const auto &a){return a.name==name;});CHECK(arg!=request.arguments.end());expected+=arg->value.content.text;at=end+1;}
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalPoolRuleMessage(request,policy,128,&rows)&&renderWrappedTerminalPoolRuleMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void refused(const TerminalPoolRuleMessageRequest &bad,const TerminalTextPolicy &policy){
    TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
    PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
    CHECK(!layoutTerminalPoolRuleMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
    CHECK(!wrapTerminalPoolRuleMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
    CHECK(!renderTerminalPoolRuleMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
    CHECK(!renderWrappedTerminalPoolRuleMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});
}
void failures(){
    for(const auto &pack:i18n::poolRuleLocalePacks())for(std::size_t key=0;key<8;++key)for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
        const auto original=requestFor(pack,static_cast<Key>(key));TerminalTextPolicy policy;policy.order=order;
        auto bad=original;bad.arguments.pop_back();refused(bad,policy);bad=original;bad.arguments.push_back(bad.arguments[0]);refused(bad,policy);
        for(std::size_t i=0;i<original.arguments.size();++i)for(unsigned poison=0;poison<7;++poison){bad=original;auto &span=bad.arguments[i].value.content;
            if(poison==0)span.invariant=false;
            if(poison==1)span.text.clear();
            if(poison==2)span.text=u8"١";
            if(poison==3)span.text="\x1b";
            if(poison==4)span.text=std::string(301,'x');
            if(poison==5)span.ascii="substituted";
            if(poison==6)span.ascii=std::string(301,'x');
            refused(bad,policy);}
    }
    auto request=requestFor(i18n::poolRuleLocalePacks()[0],Key::BeforeBoth);TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=4096;TerminalMessageLine line;
    for(auto &arg:request.arguments)arg.value.content.text=std::string(300,'x');
    CHECK(layoutTerminalPoolRuleMessage(request,policy,&line)&&!line.line.line.clipped);
    policy.columns=19;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalPoolRuleMessage(request,policy,128,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    request=requestFor(i18n::poolRuleLocalePacks()[0],Key::BeforeUnset);policy.columns=1024;request.language="en-US";CHECK(layoutTerminalPoolRuleMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    request.key=Key::Count;refused(request,policy);request=requestFor(i18n::poolRuleLocalePacks()[0],Key::BeforeUnset);request.language="en@private";refused(request,policy);
    CHECK(!layoutTerminalPoolRuleMessage({},policy,nullptr)&&!wrapTerminalPoolRuleMessage({},policy,1,nullptr)&&!renderTerminalPoolRuleMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalPoolRuleMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack complete pool review envelopes with invariant nonempty requests and literal missing endpoints");}
