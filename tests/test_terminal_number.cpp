// Synthetic exact presentation composition, not financial source or native terminal/GPU qualification.
#include "product/terminal_number.h"
#include "product/localization.h"
#include "product_test_util.h"
using namespace om;
namespace {
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
void matrix(){
    for(const auto &locale:presentationFormattingLocales())for(const auto &language:i18n::localePacks())for(auto digits:{PresentationDigits::Latin,PresentationDigits::Locale}){
        TerminalQuantityRequest request;request.number.decimal="-9007199254740993.015";request.number.formattingLocale=locale;request.number.language=std::string(language.id);request.number.digits=digits;request.label="PRL";request.tone=Tone::Canary;
        TerminalQuantity value;CHECK(formatTerminalQuantity(request,&value)&&value.number.ascii=="-9007199254740993.02"&&value.number.rounded&&value.segment.content.ascii==value.number.ascii+" PRL");
        CHECK(!value.segment.content.invariant&&value.segment.content.atom==TerminalTextAtom::LeftToRight&&value.segment.tone==Tone::Canary);
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=256;ColoredTerminalLine line;
            CHECK(layoutTerminalSegments({value.segment},policy,&line));
            const auto expected=line.line.fallback==TerminalTextFallback::None?value.segment.content.text:value.segment.content.ascii;
            CHECK(unisolate(line.line.text)==expected&&!line.line.clipped&&line.line.cells>0);
            const auto fullWidth=line.line.cells;policy.columns=fullWidth-1;
            CHECK(layoutTerminalSegments({value.segment},policy,&line)&&line.line.text.empty()&&line.line.cells==0&&line.line.clipped);
            policy.columns=256;std::vector<TerminalTextLine> rows;CHECK(wrapTerminalText({value.segment.content},policy,1,&rows)&&rows.size()==1&&unisolate(rows[0].text)==expected);
            policy.columns=fullWidth-1;rows={{"unchanged",0,false,false,TerminalTextFallback::None}};
            CHECK(!wrapTerminalText({value.segment.content},policy,64,&rows)&&rows[0].text=="unchanged");
            policy.columns=256;
            CHECK(layoutTerminalSegments({{{u8"قيمة (","value (",false},Tone::Green},value.segment,{{u8") אב"," ending",false},Tone::Red}},policy,&line));
            std::string quantity;for(const auto &run:line.runs)if(!run.content.control&&run.content.sourceSpan==1){quantity+=run.content.text;CHECK(run.tone==Tone::Canary);}
            CHECK(quantity==expected);
            policy.columns=fullWidth+3;TerminalTextLine padded;
            CHECK(padTerminalText({value.segment.content},policy,TerminalTextAlignment::Right,&padded)&&padded.cells==fullWidth+3&&unisolate(padded.text)=="   "+expected);
        }
    }
}
void boundaries(){
    TerminalQuantityRequest request;request.number.decimal="1.225";request.label="USD";TerminalQuantity value;CHECK(formatTerminalQuantity(request,&value)&&value.number.ascii=="1.22"&&value.segment.content.ascii=="1.22 USD");
    for(const std::string &label:std::vector<std::string>{"", "PRL", "USDT", "USD", "kWh", "W", "%", "m/s",std::string(32,'X')}){
        request.label=label;CHECK(formatTerminalQuantity(request,&value)&&value.segment.content.ascii=="1.22"+(label.empty()?std::string{}:" "+label));}
    TerminalQuantity before;before.number.text="unchanged";before.segment.content.text="unchanged";
    for(const std::string &bad:std::vector<std::string>{"USD EUR","\x1b",u8"美元","x\ny",std::string("x\0y",3),std::string(33,'X')}){
        request.label=bad;value=before;CHECK(!formatTerminalQuantity(request,&value)&&value.number.text=="unchanged"&&value.segment.content.text=="unchanged");}
    request.label="USD";request.number.decimal="NaN";value=before;CHECK(!formatTerminalQuantity(request,&value)&&value.number.text=="unchanged");
    request.number.decimal="1";request.tone=static_cast<Tone>(99);CHECK(!formatTerminalQuantity(request,&value)&&value.number.text=="unchanged");
    CHECK(!formatTerminalQuantity({},nullptr));
    TerminalTextPolicy policy;TerminalTextLine line{"unchanged",0,false,false,TerminalTextFallback::None};
    CHECK(!layoutTerminalText({TerminalTextSpan{"1.0","1.0",false,static_cast<TerminalTextAtom>(99)}},policy,&line)&&line.text=="unchanged");
    // Invariant ASCII remains a different contract even after atomic human presentation is available.
    CHECK(!layoutTerminalText({TerminalTextSpan{u8"١٫٢٣ USD","1.23 USD",true,TerminalTextAtom::LeftToRight}},policy,&line)&&line.text=="unchanged");
}
}
int main(){boundaries();matrix();return finishTests("typed exact localized quantities and explicit invariant unit tokens (no native activation)");}
