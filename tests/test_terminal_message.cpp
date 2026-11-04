// Typed common-message composition only; not full domains, plural sentences or ordinary activation.
#include "product/terminal_message.h"
#include "product/terminal_number.h"
#include "product/terminal_message_internal.h"
#include "product_test_util.h"
using namespace om;
namespace {
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void matrix(){
    TerminalQuantityRequest number;number.number.decimal="9007199254740993.015";number.number.formattingLocale="ar-EG";number.number.digits=PresentationDigits::Locale;number.label="PRL";number.tone=Tone::Donation;
    TerminalQuantity value;CHECK(formatTerminalQuantity(number,&value));
    for(const auto &pack:i18n::localePacks())for(std::size_t key=0;key<i18n::messageKeyCount;++key)for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})
        for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,7u,79u,256u}){
            TerminalMessageRequest request;request.language=std::string(pack.id);request.key=static_cast<i18n::MessageKey>(key);request.literalTone=Tone::Canary;
            if(request.key==i18n::MessageKey::SourceValue)request.arguments={{"source",{{"fixture",{},true},Tone::Green}},{"value",value.segment}};
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;TerminalMessageLine line;
            CHECK(layoutTerminalMessage(request,policy,&line));const bool recover=order==TerminalTextOrder::Ascii||!terminalTextBackend().unicode;
            CHECK(line.metadata.renderedLanguage==(recover?"en":pack.id)&&line.metadata.requestedLanguage==pack.id&&!line.metadata.languageFallback&&line.metadata.requestedReview==pack.review);
            CHECK(line.metadata.renderedReview==(recover?i18n::ReviewStatus::Source:pack.review)&&line.metadata.asciiRecovery==(order==TerminalTextOrder::Ascii)&&line.metadata.backendFallback==(order!=TerminalTextOrder::Ascii&&!terminalTextBackend().unicode));
            for(const auto &run:line.line.runs)if(!run.content.control&&!run.content.padding){CHECK(run.content.sourceSpan<line.metadata.sources.size());const auto &source=line.metadata.sources[run.content.sourceSpan];
                CHECK(run.tone==(source=="source"?Tone::Green:source=="value"?Tone::Donation:Tone::Canary));}
            if(width==256){i18n::MessageArguments arguments;if(!request.arguments.empty())arguments={{"source","fixture"},{"value",recover?value.segment.content.ascii:value.segment.content.text}};
                std::string expected;CHECK(i18n::formatMessage(line.metadata.renderedLanguage,request.key,arguments,&expected));
                // Visual RTL literals reorder, but protected numeric/unit values remain exact in their own run.
                if(order!=TerminalTextOrder::Visual||recover)CHECK(unisolate(line.line.line.text)==expected);
                if(!request.arguments.empty()){std::string quantity;for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]=="value")quantity+=run.content.text;
                    CHECK(quantity==(recover?value.segment.content.ascii:value.segment.content.text));}
            }
            PaintedTerminalMessageLine painted;CHECK(renderTerminalMessage(request,policy,ColorLevel::TrueColor,&painted)&&unpaint(painted.text)==line.line.line.text&&painted.metadata.sources==line.metadata.sources);
        }
}
void boundaries(){
    TerminalMessageRequest request;request.key=i18n::MessageKey::SourceValue;request.language="fr";request.arguments={{"source",{{"{value} %s",{},true},Tone::Green}},{"value",{{"1.23 USD",{},true},Tone::Donation}}};
    TerminalTextPolicy policy;policy.columns=80;TerminalMessageLine line;CHECK(layoutTerminalMessage(request,policy,&line)&&line.line.line.text==u8"{value} %s: 1.23 USD"&&line.metadata.renderedLanguage=="en"&&line.metadata.asciiRecovery);
    policy.order=TerminalTextOrder::Logical;CHECK(layoutTerminalMessage(request,policy,&line));if(terminalTextBackend().unicode)CHECK(unisolate(line.line.line.text)=="{value} %s : 1.23 USD"&&line.metadata.renderedLanguage=="fr");
    request.language="en-US";CHECK(layoutTerminalMessage(request,policy,&line)&&line.metadata.languageFallback&&!line.metadata.requestedReview&&line.metadata.renderedLanguage=="en");
    TerminalMessageLine before;before.line.line.text="unchanged";before.metadata.renderedLanguage="previous";
    for(unsigned mode=0;mode<8;++mode){auto bad=request;if(mode==0)bad.arguments.pop_back();if(mode==1)bad.arguments.push_back(bad.arguments[0]);if(mode==2)bad.arguments[0].name="unexpected";
        if(mode==3)bad.arguments[0].value.content.text="\x1b[31m";
        if(mode==4)bad.language="en@private";
        if(mode==5)bad.key=static_cast<i18n::MessageKey>(999);
        if(mode==6)bad.literalTone=static_cast<Tone>(99);
        if(mode==7)bad.arguments[0].value.tone=static_cast<Tone>(99);
        line=before;CHECK(!layoutTerminalMessage(bad,policy,&line)&&line.line.line.text=="unchanged"&&line.metadata.renderedLanguage=="previous");}
    request.language="en";request.arguments[0].value.content=TerminalTextSpan{std::string(4093,'x')};request.arguments[1].value.content=TerminalTextSpan{"y"};policy.order=TerminalTextOrder::Ascii;policy.columns=4096;
    CHECK(layoutTerminalMessage(request,policy,&line)&&line.line.line.text.size()==4096&&!line.line.line.clipped);
    request.arguments[0].value.content.text+='x';line=before;CHECK(!layoutTerminalMessage(request,policy,&line)&&line.line.line.text=="unchanged");
    request={};request.language="ja";policy.columns=3;TerminalMessageRows rows;CHECK(wrapTerminalMessage(request,policy,10,&rows)&&rows.metadata.renderedLanguage=="en");
    PaintedTerminalMessageRows painted;CHECK(renderWrappedTerminalMessage(request,policy,10,ColorLevel::None,&painted)&&painted.rows.size()==rows.rows.size());
    for(std::size_t i=0;i<painted.rows.size();++i)CHECK(painted.rows[i]==rows.rows[i].line.text);
    painted.rows={"unchanged"};painted.metadata.renderedLanguage="previous";CHECK(!renderWrappedTerminalMessage(request,policy,1,ColorLevel::None,&painted)&&painted.rows==std::vector<std::string>{"unchanged"}&&painted.metadata.renderedLanguage=="previous");
    CHECK(!layoutTerminalMessage({},policy,nullptr)&&!wrapTerminalMessage({},policy,1,nullptr)&&!renderTerminalMessage({},policy,ColorLevel::None,nullptr));
    TerminalTextSpan literal{u8"保存",{},false,TerminalTextAtom::None,TerminalTextRecovery::UnicodeOnly};TerminalTextLine raw{"unchanged",0,false,false,TerminalTextFallback::None};
    CHECK(!layoutTerminalText({literal},policy,&raw)&&raw.text=="unchanged");policy.order=TerminalTextOrder::Logical;
    const bool supported=terminalTextBackend().unicode;CHECK(layoutTerminalText({literal},policy,&raw)==supported);
    if(supported){TerminalMessageRequest arabic;arabic.language="ar";policy.order=TerminalTextOrder::Visual;policy.direction=TerminalTextDirection::RightToLeft;policy.columns=80;
        CHECK(layoutTerminalMessage(arabic,policy,&line)&&line.line.line.text==u8"ظفح"&&line.metadata.renderedLanguage=="ar"&&line.metadata.renderedReview==i18n::ReviewStatus::MachineAssisted&&line.metadata.rtl);}
    literal.ascii="Save";CHECK(!layoutTerminalText({literal},policy,&raw));literal.ascii.clear();literal.recovery=static_cast<TerminalTextRecovery>(99);CHECK(!layoutTerminalText({literal},policy,&raw));
    // Recovering a typed argument never hides invalid original controls or invents a missing ASCII value.
    request.key=i18n::MessageKey::SourceValue;request.arguments={{"source",{{"\x1b", "safe",false},Tone::Plain}},{"value",{{"value"},Tone::Plain}}};policy.order=TerminalTextOrder::Ascii;
    CHECK(!layoutTerminalMessage(request,policy,&line));request.arguments[0].value.content=TerminalTextSpan{u8"字",{},false,TerminalTextAtom::None,TerminalTextRecovery::UnicodeOnly};CHECK(!layoutTerminalMessage(request,policy,&line));
}
void templateBridge(){
    TerminalMessageRequest request;request.language="fr";request.arguments={{"value",{{"1",{},true},Tone::Canary}}};
    terminal_message_detail::Templates valid;valid.english={"Value: {value}","en",i18n::ReviewStatus::Source,false,false};valid.requested={"Valeur : {value}","fr",i18n::ReviewStatus::MachineAssisted,false,false};valid.requestedReview=i18n::ReviewStatus::MachineAssisted;
    for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical}){TerminalTextPolicy policy;policy.order=order;policy.columns=80;TerminalMessageLine line;
        CHECK(terminal_message_detail::layoutTemplate(request,valid,policy,&line));
        for(unsigned mode=0;mode<7;++mode){auto malformed=valid;
            if(mode==0)malformed.requested.text="Valeur : {missing}";
            if(mode==1)malformed.requested.text="Valeur : {value";
            if(mode==2)malformed.english.text="Value: {value";
            if(mode==3)malformed.requested.text="{value} {value}";
            if(mode==4)malformed.english.locale="fr";
            if(mode==5)malformed.requested.text="\x1b[31m{value}";
            if(mode==6)malformed.requested.text="";
            line.line.line.text="unchanged";line.metadata.renderedLanguage="previous";
            CHECK(!terminal_message_detail::layoutTemplate(request,malformed,policy,&line)&&line.line.line.text=="unchanged"&&line.metadata.renderedLanguage=="previous");
        }
    }
}
}
int main(){matrix();boundaries();templateBridge();return finishTests("typed named common-message arguments and whole-template English recovery (not native activation)");}
