// Public descriptions and original numeric-request bytes only; never owner receipts, consent or numeric admission.
#include "product/terminal_settings_message.h"
#include "product/energy_authorization_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::EnergyAuthorizationValueMessageKey;
const std::array<std::string,31> english={{"{field}: {value}","{field}: {value}","{field} = {input}_","ON","OFF","unavailable","Ok","Missing / disabled defaults","Invalid","Conflict","Storage unavailable","Clock fault","Selection mismatch","Disabled","NeedsReview","System entropy unavailable","Unknown","frozen active plan","not active","starting / awaiting helper","paused / recovery review required","unavailable / preserve local history","none","PENDING - never auto-create again","ready / immutable identity and retention","Current capture: {state}","Current capture: {state} | --no-energy overrides provisioning","Last result: {state}","Enrollment: {state}","[x] Reviewed exact selection / private state; Space acknowledges.","[ ] Reviewed exact selection / private state; Space acknowledges."}};
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::energyAuthorizationValueMessageKeyCount==31&&i18n::energyAuthorizationValueLocalePacks().size()==16);std::set<std::string> identities;
    for(std::size_t key=0;key<31;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::energyAuthorizationValueMessageKeyName(k))).second&&i18n::energyAuthorizationValueMessageKeyName(k).find("energy_authorization.value.")==0&&i18n::energyAuthorizationValueMessage("en",k).text==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::energyAuthorizationValueLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<31;++key){const auto message=i18n::energyAuthorizationValueMessage(pack.id,static_cast<Key>(key));CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            for(const auto *token:{"PENDING","--no-energy","[x]","[ ]","Space"})if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::energyAuthorizationValueEnglish(Key::FieldRequest,{{"value","60000"},{"field","Selection ms [500..60000]"}})=="Selection ms [500..60000]: 60000");
    CHECK(i18n::energyAuthorizationValueEnglish(Key::EditInput,{{"input","{field}"},{"field","{input}"}})=="{input} = {field}_");
    CHECK(i18n::energyAuthorizationValueEnglish(Key::CaptureCli,{{"state","not active"}})=="Current capture: not active | --no-energy overrides provisioning");
    for(std::size_t key=3;key<25;++key)CHECK(i18n::energyAuthorizationValueEnglish(static_cast<Key>(key))==english[key]);
    CHECK(i18n::energyAuthorizationValueEnglish(Key::AckChecked)==english[29]&&i18n::energyAuthorizationValueEnglish(Key::AckUnchecked)==english[30]);
    CHECK(i18n::energyAuthorizationValueEnglish(Key::EditInput,{{"input",""},{"field","x"}})=="x = _");
    CHECK(!i18n::energyAuthorizationValueEnglish(Key::LastResult,{{"state",std::string(256,'x')}}).empty());
    for(const auto &text:std::vector<std::string>{"",u8"ع","\x1b",std::string(257,'x')})CHECK(i18n::energyAuthorizationValueEnglish(Key::LastResult,{{"state",text}}).empty());
    CHECK(i18n::energyAuthorizationValueEnglish(Key::FieldRequest,{{"field","x"},{"value",std::string(65,'x')}}).empty());
    CHECK(i18n::energyAuthorizationValueEnglish(Key::FieldRequest,{{"field","x"}}).empty()&&i18n::energyAuthorizationValueEnglish(Key::FieldRequest,{{"field","x"},{"field","y"}}).empty()&&i18n::energyAuthorizationValueEnglish(Key::On,{{"extra","x"}}).empty());
    CHECK(i18n::energyAuthorizationValueMessageKeyName(Key::Count).empty()&&i18n::energyAuthorizationValueMessage("en",Key::Count).text.empty()&&i18n::energyAuthorizationValueEnglish(Key::Count).empty());
}
void native(){
    EnergyAuthorizationPanel panel;auto saved=std::make_shared<EnergyAuthorizationSnapshot>();saved->state=EnergyAuthorizationOwnerState::Ready;saved->generation=saved->revision=1;
    EnergyAuthorizationReply reply;reply.status=EnergyAuthorizationStatus::Ok;reply.token="synthetic-token";reply.document=EnergyAuthorizationDocument{};saved->reply=reply;
    EnergyAuthorizationAccess access;const auto contains=[&](const std::string &text){const auto rows=panel.lines(access);return std::find(rows.begin(),rows.end(),text)!=rows.end();};
    panel.open(saved);
    for(unsigned status=0;status<11;++status){saved->reply->status=static_cast<EnergyAuthorizationStatus>(status<10?status:99);CHECK(contains("Last result: "+english[6+status]));}
    saved->reply->status=EnergyAuthorizationStatus::Ok;
    for(bool cli:{false,true})for(bool active:{false,true}){saved->cliNoEnergy=cli;access.captureActive=active;access.captureSnapshot={};
        const auto suffix=cli?" | --no-energy overrides provisioning":"";CHECK(contains(std::string("Current capture: ")+(active?"frozen active plan":"not active")+suffix));
        for(const auto state:{EnergyCaptureState::Collecting,EnergyCaptureState::Starting,EnergyCaptureState::NeedsReview,EnergyCaptureState::Faulted,EnergyCaptureState::ClockFault,EnergyCaptureState::Stopped}){
            auto capture=std::make_shared<EnergyCaptureSnapshot>();capture->state=state;access.captureSnapshot=[capture]{return capture;};
            const auto expected=state==EnergyCaptureState::Collecting?"frozen active plan":state==EnergyCaptureState::Starting?"starting / awaiting helper":state==EnergyCaptureState::NeedsReview?"paused / recovery review required":state==EnergyCaptureState::Faulted||state==EnergyCaptureState::ClockFault?"unavailable / preserve local history":"not active";
            CHECK(contains(std::string("Current capture: ")+expected+suffix));}}
    for(const auto enrollment:{EnergyEnrollment::None,EnergyEnrollment::Pending,EnergyEnrollment::Ready}){saved->reply->document->enrollment=enrollment;
        CHECK(contains(std::string("Enrollment: ")+(enrollment==EnergyEnrollment::None?"none":enrollment==EnergyEnrollment::Pending?"PENDING - never auto-create again":"ready / immutable identity and retention")));}
    saved->reply->document.reset();CHECK(contains("Enrollment: unavailable"));saved->reply->document=EnergyAuthorizationDocument{};
    for(bool on:{false,true})for(bool compact:{false,true}){panel.draftPolicy.persistenceAuthorized=on;panel.draftLimits.compaction=compact?std::optional<EnergyCompactionPolicy>(EnergyCompactionPolicy{2,32768}):std::nullopt;
        CHECK(contains(std::string("> Persist board-energy history: ")+(on?"ON":"OFF")));
        CHECK(contains(std::string("  Exact compaction (new store): ")+(compact?"ON":"OFF")));
        CHECK(contains(std::string("  Compact after batches [1..raw]: ")+(compact?"2":"unavailable")));
        CHECK(contains(std::string("  Exact segments [1..32768]: ")+(compact?"32768":"unavailable")));}
    panel.page=EnergyAuthorizationPage::Edit;panel.field=1;for(const auto &input:std::vector<std::string>{"","0","60000","999999999"}){panel.input=input;CHECK(contains("Selection ms [500..60000] = "+input+"_"));}
    panel.page=EnergyAuthorizationPage::Review;panel.ackReview=true;for(bool acknowledged:{false,true}){panel.acknowledged=acknowledged;CHECK(contains(english[acknowledged?29:30]));}
    CHECK(!panel.submittedTicket&&panel.current==saved&&panel.baseline==saved&&!saved->uncertain);
}
TerminalEnergyAuthorizationValueMessageRequest requestFor(const i18n::EnergyAuthorizationValueLocalePack &pack,Key key){
    TerminalEnergyAuthorizationValueMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;std::vector<std::string_view> names;
    CHECK(i18n::templateParameters(i18n::energyAuthorizationValueMessage(pack.id,key).text,&names));
    for(const auto name:names){TerminalSegment value{{"60000",{},true},Tone::Red};
        if(name=="field"){const bool human=key==Key::FieldHuman;value={{std::string(i18n::energyAuthorizationFieldMessage(pack.id,human?i18n::EnergyAuthorizationFieldMessageKey::PersistHistory:i18n::EnergyAuthorizationFieldMessageKey::Selection).text),human?"Persist board-energy history":"Selection ms [500..60000]"},Tone::Canary};}
        else if(name=="state"||(key==Key::FieldHuman&&name=="value")){const auto state=name=="state"?Key::CaptureFrozen:Key::On;
            value={{std::string(i18n::energyAuthorizationValueMessage(pack.id,state).text),name=="state"?"frozen active plan":"ON"},Tone::Red};}
        request.arguments.insert(request.arguments.begin(),{std::string(name),std::move(value)});}
    return request;
}
void matrix(){
    for(const auto &pack:i18n::energyAuthorizationValueLocalePacks())for(std::size_t key=0;key<31;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalEnergyAuthorizationValueMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:name=="field"?Tone::Canary:Tone::Red));}}
            for(const auto &arg:request.arguments)if(arg.value.content.invariant){std::string actual;for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]==arg.name)actual+=run.content.text;CHECK(actual.empty()||actual==arg.value.content.text);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalEnergyAuthorizationValueMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){const auto text=i18n::energyAuthorizationValueMessage(recover?"en":pack.id,request.key).text;std::string expected;std::size_t at=0;
                while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){expected+=text.substr(at);break;}expected+=text.substr(at,start-at);const auto end=text.find('}',start);const auto name=text.substr(start+1,end-start-1);
                    const auto arg=std::find_if(request.arguments.begin(),request.arguments.end(),[&](const auto &a){return a.name==name;});CHECK(arg!=request.arguments.end());const auto &span=arg->value.content;expected+=recover?(span.ascii.empty()?span.text:span.ascii):span.text;at=end+1;}
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=29;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalEnergyAuthorizationValueMessage(request,policy,128,&rows)&&renderWrappedTerminalEnergyAuthorizationValueMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=29);}
    }
}
void refused(const TerminalEnergyAuthorizationValueMessageRequest &bad,const TerminalTextPolicy &policy){
    TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
    PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
    CHECK(!layoutTerminalEnergyAuthorizationValueMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
    CHECK(!wrapTerminalEnergyAuthorizationValueMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
    CHECK(!renderTerminalEnergyAuthorizationValueMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
    CHECK(!renderWrappedTerminalEnergyAuthorizationValueMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});
}
void failures(){
    for(const auto &pack:i18n::energyAuthorizationValueLocalePacks())for(std::size_t key=0;key<31;++key)for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
        const auto original=requestFor(pack,static_cast<Key>(key));TerminalTextPolicy policy;policy.order=order;auto bad=original;
        bad.arguments.push_back({"extra",{{"x",{},true},Tone::Red}});refused(bad,policy);
        if(!original.arguments.empty()){bad=original;bad.arguments.pop_back();refused(bad,policy);bad=original;bad.arguments.push_back(bad.arguments[0]);refused(bad,policy);}
        for(std::size_t i=0;i<original.arguments.size();++i)for(unsigned poison=0;poison<8;++poison){bad=original;auto &span=bad.arguments[i].value.content;const auto maximum=span.invariant?64:256;
            if(poison==0)span.invariant=!span.invariant;
            if(poison==1){if(bad.arguments[i].name=="input")span.text=u8"١";else span.text.clear();}
            if(poison==2)span.text="\x1b";
            if(poison==3)span.text=std::string(maximum+1,'x');
            if(poison==4)span.ascii=std::string(maximum+1,'x');
            if(poison==5){span.text="\xC3";span.ascii="safe";}
            if(poison==6)span.ascii="\x1b";
            if(poison==7){if(span.invariant)span.ascii="substituted";else span.text.clear();}
            refused(bad,policy);}
    }
    auto request=requestFor(i18n::energyAuthorizationValueLocalePacks()[0],Key::EditInput);TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=1024;TerminalMessageLine line;
    for(auto &arg:request.arguments)if(arg.name=="field")arg.value.content.text=arg.value.content.ascii="x";
    for(const auto &input:std::vector<std::string>{"","{input}",std::string(64,'x')}){for(auto &arg:request.arguments)if(arg.name=="input")arg.value.content.text=input;CHECK(layoutTerminalEnergyAuthorizationValueMessage(request,policy,&line)&&unisolate(line.line.line.text)=="x = "+input+"_");}
    request=requestFor(i18n::energyAuthorizationValueLocalePacks()[0],Key::FieldRequest);for(auto &arg:request.arguments)if(arg.name=="value")arg.value.content.text=std::string(64,'x');policy.columns=19;
    PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalEnergyAuthorizationValueMessage(request,policy,128,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(layoutTerminalEnergyAuthorizationValueMessage(request,policy,&line));for(const auto &run:line.line.runs)if(!run.content.control)CHECK(line.metadata.sources[run.content.sourceSpan]!="value");
    policy.columns=1024;request=requestFor(i18n::energyAuthorizationValueLocalePacks()[0],Key::On);request.language="en-US";CHECK(layoutTerminalEnergyAuthorizationValueMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    request.key=Key::Count;refused(request,policy);request.key=Key::On;request.language="en@private";refused(request,policy);request.language="en";request.literalTone=static_cast<Tone>(99);refused(request,policy);
    CHECK(!layoutTerminalEnergyAuthorizationValueMessage({},policy,nullptr)&&!wrapTerminalEnergyAuthorizationValueMessage({},policy,1,nullptr)&&!renderTerminalEnergyAuthorizationValueMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalEnergyAuthorizationValueMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack persistence request/input rows and independent human status, capture and enrollment descriptions");}
