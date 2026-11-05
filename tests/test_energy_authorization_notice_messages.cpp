// Native memory-only notice fixture; mock callbacks provide no durable receipt, consent or capture plan.
#include "product/terminal_settings_message.h"
#include "product/energy_authorization_settings.h"
#include "product_test_util.h"
#include <set>
using namespace om;
namespace {
using Key=i18n::EnergyAuthorizationNoticeMessageKey;
std::shared_ptr<EnergyAuthorizationSnapshot> ready(){auto s=std::make_shared<EnergyAuthorizationSnapshot>();s->generation=s->revision=1;s->state=EnergyAuthorizationOwnerState::Ready;
    EnergyAuthorizationReply reply;reply.status=EnergyAuthorizationStatus::Ok;reply.token="synthetic-token";reply.document=EnergyAuthorizationDocument{};s->reply=reply;return s;}
void native(){
    using P=EnergyAuthorizationPage;EnergyAuthorizationPanel panel;EnergyAuthorizationAccess access;unsigned submissions=0,acknowledgements=0;
    access.submit=[&](uint64_t,uint64_t,const EnergyAuthorizationCommand &){++submissions;return EnergyHistoryAdmission::Queued;};
    access.acknowledge=[&](uint64_t,uint64_t){++acknowledgements;return EnergyHistoryAdmission::Queued;};
    const auto notice=[&](Key key){CHECK(panel.message==i18n::energyAuthorizationNoticeEnglish(key));};
    panel.open();panel.key('\r');notice(Key::NoEditable);
    panel.page=P::Edit;panel.input.clear();panel.key('\r');notice(Key::InvalidInteger);CHECK(panel.page==P::Edit);
    panel.field=1;panel.input="60000";panel.key('\r');notice(Key::DraftOnly);CHECK(panel.draftPolicy.selectionMs==60000&&panel.page==P::Browse);
    panel.page=P::Edit;panel.key(27);notice(Key::EditCancelled);
    panel.page=P::Review;panel.key(27);notice(Key::ReviewCancelled);
    panel.page=P::Review;panel.reviewedCommand.action=EnergyAuthorizationAction::Enroll;panel.key('\r');notice(Key::NeedRevealAck);
    panel.reviewedCommand.action=EnergyAuthorizationAction::Save;panel.key('\r');notice(Key::ReviewUnavailable);
    panel.open(ready());panel.page=P::Review;panel.reviewed=panel.current;panel.ackReview=panel.reveal=panel.acknowledged=true;
    panel.key('\r',access);notice(Key::InspectionAck);CHECK(acknowledgements==1&&submissions==0&&!panel.submittedTicket);
    panel.open(ready());panel.page=P::Review;panel.reviewed=panel.current;panel.reviewedCommand.action=EnergyAuthorizationAction::Save;
    panel.key('\r',access);notice(Key::Queued);CHECK(submissions==1&&panel.submittedTicket==1&&panel.submittedGeneration==1);
    auto rejected=access;rejected.submit=[](uint64_t,uint64_t,const EnergyAuthorizationCommand &){return EnergyHistoryAdmission::NotReady;};
    panel.open(ready());panel.page=P::Review;panel.reviewed=panel.current;panel.reviewedCommand.action=EnergyAuthorizationAction::Save;
    panel.key('\r',rejected);notice(Key::ReviewChanged);CHECK(!panel.submittedTicket);
    panel.open(ready());panel.draftPolicy.selectionMs=2000;panel.key('D');notice(Key::DraftDiscarded);CHECK(!panel.dirty());
    panel.draftPolicy.selectionMs=2000;panel.key('F',access);notice(Key::DiscardBeforeInspect);CHECK(submissions==1&&panel.dirty());
    panel.open(ready());panel.key('F',access);notice(Key::InspectionQueued);CHECK(submissions==2);
    panel.open(ready());panel.key('F',rejected);notice(Key::InspectionUnavailable);
    panel.open();panel.key('A',access);notice(Key::NoUnknown);
    auto enrolled=ready();enrolled->reply->document->enrollment=EnergyEnrollment::Ready;panel.open(enrolled);panel.field=6;panel.key('\r');notice(Key::RetentionFixed);
    panel.open(ready());panel.field=12;panel.key('\r');notice(Key::NeedCompaction);CHECK(!panel.draftLimits.compaction);
    panel.open(ready());panel.key('V');notice(Key::BaselineUnavailable);
    panel.open(ready());panel.key('E',access);notice(Key::EnrollmentRequired);CHECK(submissions==2);
    panel.key('C',access);notice(Key::PendingInspectRequired);CHECK(submissions==2);
    panel.open(ready());panel.page=P::Review;panel.reviewed=panel.current;panel.acknowledged=true;
    auto changed=ready();changed->revision=2;panel.update(changed);notice(Key::OwnerChanged);CHECK(panel.page==P::Browse&&!panel.reviewed&&!panel.acknowledged);
    panel.open(ready());panel.submittedTicket=5;panel.submittedGeneration=1;auto unknown=ready();unknown->lastTicket=5;unknown->uncertain=std::make_shared<EnergyAuthorizationRequest>();panel.update(unknown);
    notice(Key::UnknownOutcome);CHECK(!panel.submittedTicket&&panel.current->uncertain);
    panel.open(ready());panel.draftPolicy.selectionMs=2000;panel.submittedTicket=5;panel.submittedGeneration=1;auto confirmed=ready();confirmed->lastTicket=5;panel.update(confirmed);
    notice(Key::ConfirmedSaved);CHECK(!panel.submittedTicket&&!panel.dirty()&&panel.current==confirmed&&submissions==2&&acknowledgements==1);
    CHECK(panel.key(3,access)==EnergyAuthorizationPanelResult::Quit);
}
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void catalog(){
    CHECK(i18n::energyAuthorizationNoticeMessageKeyCount==23&&i18n::energyAuthorizationNoticeLocalePacks().size()==16);std::set<std::string> identities;
    const std::array<std::string,23> english={{"Owner changed; review cancelled. Review current state again.","UNKNOWN outcome. F loads current state; A reviews the retained request. No automatic retry.","CONFIRMED saved. Next explicit parent only; current capture plan stays frozen.","Edit cancelled.","Enter a bounded whole number; blank is not zero.","Draft only. V reviews; Esc can discard.","Review cancelled; nothing queued.","I reveals the selected/private original; Space acknowledges after review.","Review unavailable.","Inspection acknowledged. Nothing retried; no capture plan issued.","QUEUED; waiting for the helper's persisted acknowledgement.","Review changed or request invalid; nothing queued.","Draft discarded; saved state unchanged.","Discard the local draft with D before inspecting current saved state.","Inspection QUEUED; no save or creation requested.","Inspection unavailable.","No inspected unknown request to acknowledge. F loads saved state first.","No editable saved snapshot. Wait for the helper or reopen after resolving its error.","Retention limits are fixed by this enrollment. Existing history is not reinterpreted.","Enable exact compaction in this new-store draft before editing its budgets.","Saved baseline unavailable or changed; discard/reload before review.","Enrollment requires a bounded explicit selected set and no existing reservation.","F must inspect the exact Pending empty store before confirmation."}};
    for(std::size_t key=0;key<23;++key){const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::energyAuthorizationNoticeMessageKeyName(k))).second&&i18n::energyAuthorizationNoticeMessageKeyName(k).find("energy_authorization.notice.")==0&&i18n::energyAuthorizationNoticeEnglish(k)==english[key]);}
    for(std::size_t p=0;p<16;++p){const auto &pack=i18n::energyAuthorizationNoticeLocalePacks()[p];const auto &common=i18n::localePacks()[p];CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
        for(std::size_t key=0;key<23;++key){const auto message=i18n::energyAuthorizationNoticeMessage(pack.id,static_cast<Key>(key));std::vector<std::string_view> parameters;
            CHECK(!message.text.empty()&&!message.fallback&&message.locale==pack.id&&message.review==pack.review&&message.rtl==pack.rtl&&i18n::validTemplateTranslation(english[key],message.text));
            CHECK(i18n::templateParameters(message.text,&parameters)&&parameters.empty());
            for(const auto *token:{"UNKNOWN","CONFIRMED","QUEUED","Pending","Esc","Space"})if(english[key].find(token)!=english[key].npos)CHECK(message.text.find(token)!=message.text.npos);
            if(key==1)CHECK(message.text.find("F")!=message.text.npos&&message.text.find("A")!=message.text.npos);
            if(key==5)CHECK(message.text.find("V")!=message.text.npos);
            if(key==7)CHECK(message.text.find("I")!=message.text.npos);
            if(key==13)CHECK(message.text.find("D")!=message.text.npos);
            if(key==16||key==22)CHECK(message.text.find("F")!=message.text.npos);}}
    CHECK(i18n::energyAuthorizationNoticeMessageKeyName(Key::Count).empty()&&i18n::energyAuthorizationNoticeMessage("en",Key::Count).text.empty()&&i18n::energyAuthorizationNoticeEnglish(Key::Count).empty());
}
void matrix(){
    for(const auto &pack:i18n::energyAuthorizationNoticeLocalePacks())for(std::size_t key=0;key<23;++key){TerminalEnergyAuthorizationNoticeMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<Key>(key);request.literalTone=Tone::Green;
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,3u,19u,79u,1024u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalEnergyAuthorizationNoticeMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.requestedLanguage==pack.id&&line.metadata.requestedReview==pack.review&&!line.metadata.languageFallback&&line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review));
            std::string joined;std::size_t cells=0;for(const auto &run:line.line.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(!run.content.cells&&run.tone==Tone::Plain);
                else CHECK(!run.content.padding&&run.content.sourceSpan<line.metadata.sources.size()&&line.metadata.sources[run.content.sourceSpan].empty()&&run.tone==Tone::Green);}
            CHECK(joined==line.line.line.text&&cells==line.line.line.cells&&cells<=width);
            PaintedTerminalMessageLine painted;CHECK(renderTerminalEnergyAuthorizationNoticeMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==joined&&painted.metadata.sources==line.metadata.sources);
            if(width==1024&&(order!=TerminalTextOrder::Visual||recover))CHECK(unisolate(joined)==i18n::energyAuthorizationNoticeMessage(recover?"en":pack.id,request.key).text&&!line.line.line.clipped);
        }
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){TerminalTextPolicy policy;policy.order=order;policy.columns=19;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
            CHECK(wrapTerminalEnergyAuthorizationNoticeMessage(request,policy,128,&rows)&&renderWrappedTerminalEnergyAuthorizationNoticeMessage(request,policy,128,ColorLevel::None,&painted)&&rows.rows.size()==painted.rows.size());
            CHECK(rows.metadata.sources==painted.metadata.sources);for(std::size_t i=0;i<rows.rows.size();++i)CHECK(rows.rows[i].line.text==painted.rows[i]&&!rows.rows[i].line.clipped&&rows.rows[i].line.cells<=19);}
    }
}
void failures(){
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(unsigned mode=0;mode<10;++mode){TerminalEnergyAuthorizationNoticeMessageRequest bad;TerminalTextPolicy policy;policy.order=order;
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
        CHECK(!layoutTerminalEnergyAuthorizationNoticeMessage(bad,policy,&line)&&line.line.line.text=="previous"&&line.metadata.renderedLanguage=="unchanged");
        CHECK(!wrapTerminalEnergyAuthorizationNoticeMessage(bad,policy,128,&rows)&&rows.metadata.renderedLanguage=="unchanged");
        CHECK(!renderTerminalEnergyAuthorizationNoticeMessage(bad,policy,ColorLevel::None,&painted)&&painted.text=="previous");
        CHECK(!renderWrappedTerminalEnergyAuthorizationNoticeMessage(bad,policy,128,ColorLevel::None,&paintedRows)&&paintedRows.rows==std::vector<std::string>{"previous"});}
    TerminalEnergyAuthorizationNoticeMessageRequest request;request.language="en-US";TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;TerminalMessageLine line;
    CHECK(layoutTerminalEnergyAuthorizationNoticeMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    policy.columns=3;request.key=Key::EnrollmentRequired;PaintedTerminalMessageRows rows;rows.rows={"previous"};CHECK(!renderWrappedTerminalEnergyAuthorizationNoticeMessage(request,policy,1,ColorLevel::None,&rows)&&rows.rows==std::vector<std::string>{"previous"});
    CHECK(!layoutTerminalEnergyAuthorizationNoticeMessage({},policy,nullptr)&&!wrapTerminalEnergyAuthorizationNoticeMessage({},policy,1,nullptr)&&!renderTerminalEnergyAuthorizationNoticeMessage({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalEnergyAuthorizationNoticeMessage({},policy,1,ColorLevel::None,nullptr));
}
}
int main(){catalog();native();matrix();failures();return finishTests("sixteen-pack local authorization notices without receipts, consent, identity or capture-plan authority");}
