// Typed autonomous rows/fields only; not rich-paragraph, physical terminal or native language activation.
#include "product/terminal_number.h"
#include "product/localization.h"
#include "product_test_util.h"
using namespace om;
namespace {
std::string plain(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;
    CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
void provenance(const ColoredTerminalLine &line,const std::vector<TerminalSegment> &spans,Tone padding){
    std::string joined;std::size_t cells=0;for(const auto &run:line.runs){joined+=run.content.text;cells+=run.content.cells;
        if(run.content.control)CHECK(!run.content.padding&&run.tone==Tone::Plain&&!run.content.cells);
        else if(run.content.padding)CHECK(run.tone==padding&&run.content.text.find_first_not_of(' ')==run.content.text.npos&&run.content.cells==run.content.text.size());
        else CHECK(run.content.sourceSpan<spans.size()&&run.tone==spans[run.content.sourceSpan].tone);
    }CHECK(joined==line.line.text&&cells==line.line.cells);
}
void matrix(){
    for(const auto &pack:i18n::localePacks())for(std::size_t key=0;key<i18n::messageKeyCount;++key)for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})
        for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{3u,7u,19u,79u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;
            const std::vector<TerminalSegment> spans={{{std::string(pack.messages[key]),std::string(i18n::localePacks()[0].messages[key]),false},Tone::Green},{{" / ",{},false},Tone::Dark},{{"USD",{},true},Tone::Canary}};
            const std::vector<TerminalTextSpan> bare={spans[0].content,spans[1].content,spans[2].content};
            std::vector<TerminalTextLine> expected;std::vector<ColoredTerminalLine> rows;CHECK(wrapTerminalText(bare,policy,128,&expected)&&wrapTerminalSegments(spans,policy,128,&rows)&&rows.size()==expected.size());
            for(std::size_t i=0;i<rows.size();++i){CHECK(rows[i].line.text==expected[i].text&&rows[i].line.cells==expected[i].cells&&!rows[i].line.clipped);provenance(rows[i],spans,Tone::Plain);}
            for(auto color:{ColorLevel::None,ColorLevel::TrueColor}){std::vector<std::string> rendered;CHECK(renderWrappedTerminalSegments(spans,policy,128,color,&rendered)&&rendered.size()==expected.size());
                for(std::size_t i=0;i<rendered.size();++i)CHECK(plain(rendered[i])==expected[i].text);}
            for(auto alignment:{TerminalTextAlignment::Left,TerminalTextAlignment::Right}){
                TerminalTextLine field;ColoredTerminalLine typed;CHECK(padTerminalText(bare,policy,alignment,&field)&&padTerminalSegments(spans,policy,alignment,&typed,Tone::Amber));
                CHECK(typed.line.text==field.text&&typed.line.cells==width&&typed.line.clipped==field.clipped);provenance(typed,spans,Tone::Amber);
                std::string rendered;CHECK(renderPaddedTerminalSegments(spans,policy,alignment,ColorLevel::TrueColor,&rendered,Tone::Amber)&&plain(rendered)==field.text);
            }
        }
}
void boundaries(){
    TerminalTextPolicy policy;policy.columns=5;std::vector<ColoredTerminalLine> rows(1);rows[0].line.text="unchanged";
    const std::vector<TerminalSegment> words={{{"one two three",{},false},Tone::Green}};
    CHECK(!wrapTerminalSegments(words,policy,2,&rows)&&rows.size()==1&&rows[0].line.text=="unchanged");
    std::vector<std::string> rendered={"unchanged"};CHECK(!renderWrappedTerminalSegments(words,policy,2,ColorLevel::TrueColor,&rendered)&&rendered==std::vector<std::string>{"unchanged"});
    CHECK(!wrapTerminalSegments({{{"123456",{},true},Tone::Canary}},policy,100,&rows)&&rows[0].line.text=="unchanged");
    CHECK(!wrapTerminalSegments({},policy,0,&rows)&&!wrapTerminalSegments({},policy,4097,&rows)&&!wrapTerminalSegments({},policy,1,nullptr));
    ColoredTerminalLine field;field.line.text="unchanged";CHECK(!padTerminalSegments(words,policy,static_cast<TerminalTextAlignment>(99),&field)&&field.line.text=="unchanged");
    CHECK(!padTerminalSegments(words,policy,TerminalTextAlignment::Left,&field,static_cast<Tone>(99))&&field.line.text=="unchanged");
    std::string output="unchanged";CHECK(!renderPaddedTerminalSegments(words,policy,TerminalTextAlignment::Left,static_cast<ColorLevel>(99),&output)&&output=="unchanged");
    CHECK(!renderWrappedTerminalSegments(words,policy,8,static_cast<ColorLevel>(99),&rendered)&&rendered[0]=="unchanged");
    CHECK(wrapTerminalSegments({},policy,1,&rows)&&rows.size()==1&&rows[0].runs.empty()&&rows[0].line.text.empty());
    CHECK(padTerminalSegments({},policy,TerminalTextAlignment::Right,&field,Tone::Dark)&&field.line.cells==5&&field.runs.size()==1&&field.runs[0].content.padding);
    provenance(field,{},Tone::Dark);
    policy.columns=1;policy.order=TerminalTextOrder::Logical;policy.direction=TerminalTextDirection::LeftToRight;
    const std::vector<TerminalSegment> maximum={{{std::string(4096,'x'),{},false},Tone::Canary}};
    CHECK(wrapTerminalSegments(maximum,policy,4096,&rows)&&rows.size()==4096);
    for(const auto &row:rows){CHECK(row.line.cells==1&&!row.line.clipped);provenance(row,maximum,Tone::Plain);}
    CHECK(renderWrappedTerminalSegments(maximum,policy,4096,ColorLevel::TrueColor,&rendered)&&rendered.size()==4096);
    std::size_t bytes=0;for(const auto &row:rendered)bytes+=row.size();CHECK(bytes<=262144);
    TerminalQuantityRequest request;request.number.decimal="1234.5";request.number.formattingLocale="ar-EG";request.number.digits=PresentationDigits::Locale;request.label="USD";request.tone=Tone::Donation;
    TerminalQuantity quantity;CHECK(formatTerminalQuantity(request,&quantity));policy.columns=20;
    const std::vector<TerminalSegment> mixed={{{u8"قيمة ","value ",false},Tone::Green},quantity.segment,{{u8" 完了"," done",false},Tone::Red}};
    CHECK(wrapTerminalSegments(mixed,policy,20,&rows));std::string numeric;
    for(const auto &row:rows){provenance(row,mixed,Tone::Plain);for(const auto &run:row.runs)if(!run.content.control&&!run.content.padding&&run.content.sourceSpan==1)numeric+=run.content.text;}
    CHECK(numeric==(terminalTextBackend().unicode?quantity.segment.content.text:quantity.segment.content.ascii));
    if(terminalTextBackend().unicode){
        policy.columns=3;
        CHECK(wrapTerminalSegments({{{u8"\u0301","?",false},Tone::Amber}},policy,1,&rows)&&rows[0].line.replacedOrphan&&rows[0].line.cells==1);
        std::string orphan;for(const auto &run:rows[0].runs)if(!run.content.control){CHECK(run.tone==Tone::Amber&&run.content.sourceSpan==0);orphan+=run.content.text;}CHECK(orphan=="?");
        rows[0].line.text="unchanged";CHECK(!wrapTerminalSegments({{{"e",{},false},Tone::Green},{{u8"\u0301","?",false},Tone::Red}},policy,5,&rows)&&rows[0].line.text=="unchanged");
        std::string combined="e";for(unsigned i=0;i<1365;++i)combined+=u8"\u20dd";
        policy.columns=4096;field.line.text="unchanged";CHECK(!padTerminalSegments({{{combined,"e",false},Tone::Green}},policy,TerminalTextAlignment::Left,&field)&&field.line.text=="unchanged");
    }
}
}
int main(){matrix();boundaries();return finishTests("typed autonomous rows and physical padded fields with explicit generated provenance");}
