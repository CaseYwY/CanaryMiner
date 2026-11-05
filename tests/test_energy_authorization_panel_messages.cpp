// Literal persistence shell only; descriptions neither grant consent nor prove storage, disclosure or capture facts.
#include "product/terminal_settings_message.h"
#include "product/energy_authorization_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::EnergyAuthorizationPanelMessageKey;
const std::array<std::string,26> english={{"Energy persistence / independent authorization","UNAVAILABLE: no authorization helper owner supplied.","Session energy collection alone never permits disk retention.","STARTING: freezing this parent's capture decision. Settings edits wait for a fresh snapshot.","Enrollment unavailable: select full explicit GPU UUIDs; idle settings does not discover GPUs.","BUSY: bounded helper request; admission is not confirmation.","IDLE: settings do not start or restart mining.","UNAVAILABLE: authorization helper failed; preserve local stores.","UNKNOWN retained original; new mutations blocked until reviewed inspection.","Unsaved draft. D discards and closes; Esc returns to editing.","Whole numbers only. Enter keeps draft; Esc cancels this edit.","REVIEW INSPECTION acknowledgement","REVIEW ENROLLMENT / creates a new private store","REVIEW CONFIRM / exact empty store only","REVIEW SAVE / next explicit parent only","GPU board sample-and-hold; gaps remain unknown. Finite budgets reject at capacity.","Exact compaction expires raw originals, not aggregate energy; no guaranteed retention days.","PRIVATE selected UUIDs, namespace, scope, session and original timestamps hidden. I reveals locally.","Saved authorization affects only the next explicit parent. No collection or plan issuance on this page.","Terminal too short.","Resize to 12+ rows.","[Q/Ctrl+C] Stop","[I] Reveal [Space] Ack [Enter] Submit [Arrows] Scroll [Esc] Cancel [Q] Stop","[Enter] Keep draft [Esc] Cancel [Q/Ctrl+C] Stop","[V] Save review [E] Enroll [C] Confirm [F] Inspect [A] Ack [I] Reveal [Arrows] Move [Esc] Back [Q] Stop","Independent opt-in; durable acknowledgement required."}};
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
std::vector<std::string> keys(std::string_view text){std::vector<std::string> result;std::size_t at=0;
    while((at=text.find('[',at))!=text.npos){const auto end=text.find(']',at);CHECK(end!=text.npos);if(end==text.npos)break;result.emplace_back(text.substr(at,end-at+1));at=end+1;}return result;}
void catalog(){
    CHECK(i18n::energyAuthorizationPanelMessageKeyCount==26&&i18n::energyAuthorizationPanelLocalePacks().size()==16);std::set<std::string> identities;
    for(std::size_t key=0;key<26;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::energyAuthorizationPanelMessageKeyName(k))).second&&i18n::energyAuthorizationPanelMessageKeyName(k).find("energy_authorization.panel.")==0&&i18n::energyAuthorizationPanelEnglish(k)==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::energyAuthorizationPanelLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<26;++key){const auto message=i18n::energyAuthorizationPanelMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            CHECK(i18n::templateParameters(message.text,&parameters)&&parameters.empty()&&keys(english[key])==keys(message.text));
            for(const auto *token:{"UNAVAILABLE","STARTING","GPU","UUIDs","BUSY","IDLE","UNKNOWN","REVIEW","INSPECTION","ENROLLMENT","CONFIRM","SAVE","PRIVATE","12"})if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);
            if(key==9)CHECK(message.text.find("D")!=message.text.npos&&message.text.find("Esc")!=message.text.npos);
            if(key==10)CHECK(message.text.find("Enter")!=message.text.npos&&message.text.find("Esc")!=message.text.npos);
            if(key==17)CHECK(message.text.find("I")!=message.text.npos);}}
    CHECK(i18n::energyAuthorizationPanelMessageKeyName(Key::Count).empty()&&i18n::energyAuthorizationPanelMessage("en",Key::Count).text.empty()&&i18n::energyAuthorizationPanelEnglish(Key::Count).empty());
}
void native(){
    EnergyAuthorizationPanel panel;EnergyAuthorizationAccess access;
    const auto has=[&](Key key){const auto rows=panel.lines(access);CHECK(std::find(rows.begin(),rows.end(),english[static_cast<std::size_t>(key)])!=rows.end());};
    panel.open();has(Key::Title);has(Key::Unavailable);has(Key::NoRetention);
    CHECK(panel.render(80,11,ColorLevel::None)==english[19]+"\n"+english[20]+"\n"+english[21]+"\n");
    auto current=std::make_shared<EnergyAuthorizationSnapshot>();current->state=EnergyAuthorizationOwnerState::Ready;current->generation=current->revision=1;
    panel.open(current);has(Key::NeedSelection);has(Key::Idle);has(Key::PrivateHidden);has(Key::NextParent);
    current->startupPending=current->pending=true;has(Key::StartupPending);has(Key::Busy);
    current->pending=false;current->state=EnergyAuthorizationOwnerState::Faulted;has(Key::HelperFailed);
    current->uncertain=std::make_shared<EnergyAuthorizationRequest>();has(Key::UnknownRetained);
    panel.page=EnergyAuthorizationPage::Leave;has(Key::Leave);
    panel.page=EnergyAuthorizationPage::Edit;has(Key::EditGuide);
    for(const auto action:{EnergyAuthorizationAction::Save,EnergyAuthorizationAction::Enroll,EnergyAuthorizationAction::Confirm}){
        panel.page=EnergyAuthorizationPage::Review;panel.reviewedCommand.action=action;panel.ackReview=false;
        has(action==EnergyAuthorizationAction::Save?Key::ReviewSave:action==EnergyAuthorizationAction::Enroll?Key::ReviewEnroll:Key::ReviewConfirm);
        has(Key::SampleHold);has(Key::CompactionExpires);
    }
    panel.ackReview=true;has(Key::ReviewInspection);panel.reveal=true;
    const auto revealed=panel.lines(access);CHECK(std::find(revealed.begin(),revealed.end(),english[17])==revealed.end());
    for(const auto page:{EnergyAuthorizationPage::Browse,EnergyAuthorizationPage::Edit,EnergyAuthorizationPage::Review}){panel.page=page;
        const auto rendered=panel.render(1024,1000,ColorLevel::None);const auto footer=page==EnergyAuthorizationPage::Browse?24u:page==EnergyAuthorizationPage::Edit?23u:22u;
        CHECK(rendered.find(english[footer]+"\n")!=rendered.npos&&rendered.find(english[25]+"\n")!=rendered.npos);}
    CHECK(!panel.submittedTicket&&panel.current==current&&current->uncertain&&current->state==EnergyAuthorizationOwnerState::Faulted);
}
void matrix(){
    for(const auto &pack:i18n::energyAuthorizationPanelLocalePacks())for(std::size_t key=0;key<26;++key){TerminalEnergyAuthorizationPanelMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<Key>(key);request.literalTone=Tone::Green;
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalEnergyAuthorizationPanelMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size()&&line.metadata.sources[run.content.sourceSpan].empty()&&run.tone==Tone::Green);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalEnergyAuthorizationPanelMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover))CHECK(unisolate(joined)==i18n::energyAuthorizationPanelMessage(recover?"en":pack.id,request.key).text&&!line.line.line.clipped);
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalEnergyAuthorizationPanelMessage(request,policy,128,&rows)&&renderWrappedTerminalEnergyAuthorizationPanelMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<10;++mode){TerminalEnergyAuthorizationPanelMessageRequest bad;TerminalTextPolicy policy;policy.order=order;
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
        CHECK(!layoutTerminalEnergyAuthorizationPanelMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalEnergyAuthorizationPanelMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalEnergyAuthorizationPanelMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalEnergyAuthorizationPanelMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    TerminalEnergyAuthorizationPanelMessageRequest request;request.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalEnergyAuthorizationPanelMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    policy.columns=3;request.key=Key::NoRetention;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalEnergyAuthorizationPanelMessage(request,policy,1,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalEnergyAuthorizationPanelMessage({},policy,nullptr)&&!wrapTerminalEnergyAuthorizationPanelMessage({},policy,1,nullptr)&&!renderTerminalEnergyAuthorizationPanelMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalEnergyAuthorizationPanelMessage({},policy,1,ColorLevel::None,nullptr));
}
} // namespace
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack literal persistence shell without consent, disclosure, durable receipts or capture-plan authority");}
