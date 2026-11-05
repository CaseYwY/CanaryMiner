// Private display only, never real identities or owner receipts. Native reveal/sanitizer remains a separate authority.
#include "product/terminal_settings_message.h"
#include "product/energy_authorization_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::EnergyAuthorizationPrivateMessageKey;
const std::array<std::string,14> english={{"Selected UUID {position}: {identity}","Selected UUID {position}: ","Namespace {identity}","Namespace ","Scope {identity}","Scope ","History begin UTC ms {instant}","Last reserved session {identity}","Last reserved session ","Channel {identity}","Channel ","ORIGINAL action {action} | UTC ms {instant}","Original CAS {identity}","Original CAS "}};
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::energyAuthorizationPrivateMessageKeyCount==14&&i18n::energyAuthorizationPrivateLocalePacks().size()==16);std::set<std::string> identities;
    for(std::size_t key=0;key<14;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::energyAuthorizationPrivateMessageKeyName(k))).second&&i18n::energyAuthorizationPrivateMessageKeyName(k).find("energy_authorization.private.")==0&&i18n::energyAuthorizationPrivateMessage("en",k).text==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::energyAuthorizationPrivateLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<14;++key){const auto message=i18n::energyAuthorizationPrivateMessage(pack.id,static_cast<Key>(key));CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            for(const auto *token:{"UUID","UTC ms","ORIGINAL","CAS"})if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);}}
    CHECK(i18n::energyAuthorizationPrivateEnglish(Key::SelectedUuid,{{"identity","{position}"},{"position","{identity}"}})=="Selected UUID {identity}: {position}");
    CHECK(i18n::energyAuthorizationPrivateEnglish(Key::SelectedUuidEmpty,{{"position","64"}})=="Selected UUID 64: ");
    for(const auto key:{Key::NamespaceEmpty,Key::ScopeEmpty,Key::LastSessionEmpty,Key::ChannelEmpty,Key::OriginalCasEmpty})CHECK(i18n::energyAuthorizationPrivateEnglish(key)==english[static_cast<std::size_t>(key)]);
    CHECK(!i18n::energyAuthorizationPrivateEnglish(Key::Namespace,{{"identity",std::string(128,'x')}}).empty());
    for(const auto &text:std::vector<std::string>{"",u8"ع","\x1b",std::string(129,'x')})CHECK(i18n::energyAuthorizationPrivateEnglish(Key::Namespace,{{"identity",text}}).empty());
    CHECK(i18n::energyAuthorizationPrivateEnglish(Key::HistoryBegin,{{"instant",std::string(65,'x')}}).empty());
    CHECK(i18n::energyAuthorizationPrivateEnglish(Key::SelectedUuid,{{"position","1"}}).empty()&&i18n::energyAuthorizationPrivateEnglish(Key::SelectedUuid,{{"position","1"},{"position","2"}}).empty()&&i18n::energyAuthorizationPrivateEnglish(Key::NamespaceEmpty,{{"identity","unused"}}).empty());
    CHECK(i18n::energyAuthorizationPrivateMessageKeyName(Key::Count).empty()&&i18n::energyAuthorizationPrivateMessage("en",Key::Count).text.empty()&&i18n::energyAuthorizationPrivateEnglish(Key::Count).empty());
}
std::string oldSafe(const std::string &text){std::string out;for(unsigned char c:text){if(out.size()==128)break;out+=c>=32&&c<=126?static_cast<char>(c):'?';}return out;}
void native(){
    for(const auto &raw:std::vector<std::string>{"","synthetic-id","{identity}{position}",std::string("x\x1b\0y",4),u8"ع",std::string(200,'x')})for(bool original:{false,true}){
        auto saved=std::make_shared<EnergyAuthorizationSnapshot>();saved->state=EnergyAuthorizationOwnerState::Ready;saved->generation=saved->revision=1;
        EnergyAuthorizationReply reply;reply.status=EnergyAuthorizationStatus::Ok;reply.token="synthetic-token";reply.document=EnergyAuthorizationDocument{};saved->reply=reply;
        auto &doc=*saved->reply->document;doc.enrollment=EnergyEnrollment::Ready;doc.privateNamespace=doc.binding.evidence.scope=doc.lastSessionId=raw;doc.binding.channels={raw};doc.binding.historyBeginUtcMs=-1800000000000LL;
        auto request=std::make_shared<EnergyAuthorizationRequest>();request->expected=raw;request->nowUtcMs=-1800000000000LL;request->action=EnergyAuthorizationAction::Save;request->devices=std::vector<std::string>(65,raw);saved->uncertain=request;
        EnergyAuthorizationPanel panel;panel.open(saved);panel.ackReview=original;EnergyAuthorizationAccess access;access.selectedDevices=std::vector<std::string>(65,original?"not-the-original":raw);
        const auto hidden=panel.lines(access);for(const auto &row:hidden)CHECK(row.rfind("Selected UUID ",0)!=0&&row.rfind("Namespace ",0)!=0&&row.rfind("Scope ",0)!=0&&row.rfind("Original CAS ",0)!=0);
        panel.reveal=true;const auto visible=panel.lines(access);const auto safe=oldSafe(raw);
        const auto has=[&](const std::string &row){return std::find(visible.begin(),visible.end(),row)!=visible.end();};
        CHECK(has("Selected UUID 1: "+safe)&&has("Selected UUID 64: "+safe)&&!has("Selected UUID 65: "+safe));
        for(const auto *prefix:{"Namespace ","Scope ","Last reserved session ","Channel "})CHECK(has(prefix+safe));
        CHECK(has("History begin UTC ms -1800000000000"));
        CHECK(has("Original CAS "+safe)==original&&has("ORIGINAL action "+std::to_string(static_cast<unsigned>(request->action))+" | UTC ms -1800000000000")==original);
        CHECK(doc.privateNamespace==raw&&doc.binding.evidence.scope==raw&&doc.lastSessionId==raw&&request->expected==raw&&!panel.submittedTicket);
    }
}
TerminalEnergyAuthorizationPrivateMessageRequest requestFor(const i18n::EnergyAuthorizationPrivateLocalePack &pack,Key key){
    TerminalEnergyAuthorizationPrivateMessageRequest request;request.language=std::string(pack.id);request.key=key;request.literalTone=Tone::Green;std::vector<std::string_view> names;
    CHECK(i18n::templateParameters(i18n::energyAuthorizationPrivateMessage(pack.id,key).text,&names));
    for(const auto name:names){TerminalSegment value{{name=="identity"?"synthetic-id":name=="action"?"3":"-1800000000000",{},true},Tone::Red};
        if(name=="position")value={{"64","64",false,TerminalTextAtom::LeftToRight},Tone::Canary};
        request.arguments.insert(request.arguments.begin(),{std::string(name),std::move(value)});}
    return request;
}
void matrix(){
    for(const auto &pack:i18n::energyAuthorizationPrivateLocalePacks())for(std::size_t key=0;key<14;++key){const auto request=requestFor(pack,static_cast<Key>(key));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalEnergyAuthorizationPrivateMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else{CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size());const auto &name=line.metadata.sources[run.content.sourceSpan];CHECK(run.tone==(name.empty()?Tone::Green:name=="position"?Tone::Canary:Tone::Red));}}
            for(const auto &arg:request.arguments){std::string actual;for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]==arg.name)actual+=run.content.text;CHECK(actual.empty()||actual==arg.value.content.text);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalEnergyAuthorizationPrivateMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover)){const auto text=i18n::energyAuthorizationPrivateMessage(recover?"en":pack.id,request.key).text;std::string expected;std::size_t at=0;
                while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){expected+=text.substr(at);break;}expected+=text.substr(at,start-at);const auto end=text.find('}',start);const auto name=text.substr(start+1,end-start-1);
                    const auto arg=std::find_if(request.arguments.begin(),request.arguments.end(),[&](const auto &a){return a.name==name;});CHECK(arg!=request.arguments.end());expected+=arg->value.content.text;at=end+1;}
                CHECK(unisolate(joined)==expected&&!line.line.line.clipped);}
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=29;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalEnergyAuthorizationPrivateMessage(request,policy,128,&rows)&&renderWrappedTerminalEnergyAuthorizationPrivateMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=29);}
    }
}
void refused(const TerminalEnergyAuthorizationPrivateMessageRequest &bad,const TerminalTextPolicy &policy){
    TerminalMessageLine line;line.line.line.text="previous";line.metadata.renderedLanguage="unchanged";TerminalMessageRows rows;rows.metadata.renderedLanguage="unchanged";
    PaintedTerminalMessageLine painted;painted.text="previous";PaintedTerminalMessageRows paintedRows;paintedRows.rows={"previous"};
    CHECK(!layoutTerminalEnergyAuthorizationPrivateMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
    CHECK(!wrapTerminalEnergyAuthorizationPrivateMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
    CHECK(!renderTerminalEnergyAuthorizationPrivateMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
    CHECK(!renderWrappedTerminalEnergyAuthorizationPrivateMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});
}
void failures(){
    for(const auto &pack:i18n::energyAuthorizationPrivateLocalePacks())for(std::size_t key=0;key<14;++key)for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
        const auto original=requestFor(pack,static_cast<Key>(key));TerminalTextPolicy policy;policy.order=order;auto bad=original;
        bad.arguments.push_back({"extra",{{"x",{},true},Tone::Red}});refused(bad,policy);
        if(!original.arguments.empty()){bad=original;bad.arguments.pop_back();refused(bad,policy);bad=original;bad.arguments.push_back(bad.arguments[0]);refused(bad,policy);}
        for(std::size_t i=0;i<original.arguments.size();++i)for(unsigned poison=0;poison<8;++poison){bad=original;auto &span=bad.arguments[i].value.content;const auto maximum=bad.arguments[i].name=="position"?256:bad.arguments[i].name=="identity"?128:64;
            if(poison==0)span.invariant=!span.invariant;
            if(poison==1)span.text.clear();
            if(poison==2)span.text="\x1b";
            if(poison==3)span.text=std::string(maximum+1,'x');
            if(poison==4)span.ascii=std::string(maximum+1,'x');
            if(poison==5){span.text="\xC3";span.ascii="safe";}
            if(poison==6)span.ascii="\x1b";
            if(poison==7){if(span.invariant)span.ascii="substituted";else span.atom=TerminalTextAtom::None;}
            refused(bad,policy);}
    }
    auto request=requestFor(i18n::energyAuthorizationPrivateLocalePacks()[0],Key::SelectedUuidEmpty);TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=1024;TerminalMessageLine line;
    request.arguments[0].value.content.text=u8"٦٤";CHECK(layoutTerminalEnergyAuthorizationPrivateMessage(request,policy,&line));
    CHECK(unisolate(line.line.line.text)==(terminalTextBackend().unicode?u8"Selected UUID ٦٤: ":"Selected UUID 64: "));
    request=requestFor(i18n::energyAuthorizationPrivateLocalePacks()[0],Key::Namespace);request.arguments[0].value.content.text=std::string(128,'x');CHECK(layoutTerminalEnergyAuthorizationPrivateMessage(request,policy,&line));
    policy.columns=19;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalEnergyAuthorizationPrivateMessage(request,policy,128,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(layoutTerminalEnergyAuthorizationPrivateMessage(request,policy,&line));for(const auto &run:line.line.runs)if(!run.content.control)CHECK(line.metadata.sources[run.content.sourceSpan]!="identity");
    policy.columns=1024;request=requestFor(i18n::energyAuthorizationPrivateLocalePacks()[0],Key::NamespaceEmpty);request.language="en-US";CHECK(layoutTerminalEnergyAuthorizationPrivateMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    request.key=Key::Count;refused(request,policy);request.key=Key::NamespaceEmpty;request.language="en@private";refused(request,policy);request.language="en";request.literalTone=static_cast<Tone>(99);refused(request,policy);
    CHECK(!layoutTerminalEnergyAuthorizationPrivateMessage({},policy,nullptr)&&!wrapTerminalEnergyAuthorizationPrivateMessage({},policy,1,nullptr)&&!renderTerminalEnergyAuthorizationPrivateMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalEnergyAuthorizationPrivateMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack private persistence envelopes with unchanged reveal, sanitization and original request boundaries");}
