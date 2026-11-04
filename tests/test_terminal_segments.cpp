// CPU presentation/provenance/color fixtures; not physical terminal/font/shape/accessibility qualification.
#include "product/terminal_segments.h"
#include "product/localization.h"
#include "product_test_util.h"
using namespace om;
namespace {
void legacyPalette(){
    const bool before=dash_detail::g_dashBoxRules;
    const std::vector<Tone> tones={Tone::Plain,Tone::Dark,Tone::Rule,Tone::Canary,Tone::Donation,Tone::Amber,Tone::Red,Tone::Green};
    const std::vector<std::string> basic={"","\x1b[90m","\x1b[90m","\x1b[93m","\x1b[97m","\x1b[33m","\x1b[91m","\x1b[92m"};
    for(bool boxes:{false,true})for(auto color:{ColorLevel::None,ColorLevel::Basic16,ColorLevel::Palette256,ColorLevel::TrueColor})
        for(std::size_t i=0;i<tones.size();++i)for(const std::string &text:std::vector<std::string>{"", "x--x",u8"中-👩‍💻"}){
            dash_detail::g_dashBoxRules=boxes;std::string expected=text;
            if(color!=ColorLevel::None&&i&& !text.empty()){
                auto open=basic[i];if(i==3&&color==ColorLevel::Palette256)open="\x1b[38;5;220m";
                if(i==4&&color==ColorLevel::Palette256)open="\x1b[38;5;230m";
                if(i==3&&color==ColorLevel::TrueColor)open="\x1b[38;2;255;210;31m";
                if(i==4&&color==ColorLevel::TrueColor)open="\x1b[38;2;255;243;176m";
                if(i==2&&boxes){expected.clear();for(char ch:text)expected+=ch=='-'?u8"─":std::string(1,ch);}
                expected=open+expected+"\x1b[0m";
            }
            CHECK(dash_detail::paint(text,tones[i],color)==expected);
        }
    dash_detail::g_dashBoxRules=before;
}
std::string unpaint(const std::string &text){
    std::string out;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){out+=text[i++];continue;}
        CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;
        while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;
        CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;
    }return out;
}
void matrix(){
    for(const auto &pack:i18n::localePacks())for(std::size_t key=0;key<i18n::messageKeyCount;++key)for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})
        for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(std::size_t width:{0u,1u,3u,7u,19u,79u}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=width;
            const std::vector<TerminalSegment> spans={{{std::string(pack.messages[key]),std::string(i18n::localePacks()[0].messages[key]),false},Tone::Green},{{" / ",{},false},Tone::Dark},{{"1.23 PRL / USD / UTC",{},true},Tone::Canary}};
            const std::vector<TerminalTextSpan> bare={spans[0].content,spans[1].content,spans[2].content};
            ColoredTerminalLine value;TerminalTextLine plain;CHECK(layoutTerminalSegments(spans,policy,&value)&&layoutTerminalText(bare,policy,&plain));
            CHECK(value.line.text==plain.text&&value.line.cells==plain.cells&&value.line.clipped==plain.clipped&&value.line.fallback==plain.fallback);
            std::string joined;std::size_t cells=0;for(const auto &run:value.runs){joined+=run.content.text;cells+=run.content.cells;
                if(run.content.control)CHECK(run.tone==Tone::Plain&&run.content.cells==0);
                else CHECK(run.content.sourceSpan<spans.size()&&run.tone==spans[run.content.sourceSpan].tone);
            }
            CHECK(joined==value.line.text&&cells==value.line.cells);
            for(auto color:{ColorLevel::None,ColorLevel::Basic16,ColorLevel::Palette256,ColorLevel::TrueColor}){
                std::string painted;CHECK(renderTerminalSegments(spans,policy,color,&painted)&&painted.size()<=65536&&unpaint(painted)==plain.text);
                if(color==ColorLevel::None)CHECK(painted==plain.text);
            }
        }
}
void focused(){
    TerminalTextPolicy policy;policy.columns=80;std::string text;ColoredTerminalLine value;
    CHECK(renderTerminalSegments({{{"---",{},false},Tone::Rule}},policy,ColorLevel::TrueColor,&text)&&unpaint(text)=="---");
    value.line.text="unchanged";CHECK(!layoutTerminalSegments({{{"safe",{},false},static_cast<Tone>(99)}},policy,&value)&&value.line.text=="unchanged");
    text="unchanged";CHECK(!renderTerminalSegments({{{"safe",{},false},Tone::Green}},policy,static_cast<ColorLevel>(99),&text)&&text=="unchanged");
    CHECK(!renderTerminalSegments({{{"\x1b[31m",{},false},Tone::Red}},policy,ColorLevel::None,&text)&&text=="unchanged");
    CHECK(!layoutTerminalSegments({},policy,nullptr)&&!renderTerminalSegments({},policy,ColorLevel::None,nullptr));
    CHECK(layoutTerminalSegments({},policy,&value)&&value.runs.empty()&&value.line.text.empty()&&value.line.cells==0);
    const std::vector<TerminalSegment> controls={{{"",{},true},Tone::Red},{{"x",{},false},Tone::Green},{{"",{},false},Tone::Canary}};
    CHECK(layoutTerminalSegments(controls,policy,&value)&&value.runs.size()==1&&value.runs[0].content.sourceSpan==1&&value.runs[0].tone==Tone::Green);
    policy.columns=4096;CHECK(layoutTerminalSegments({{{std::string(4096,'x'),{},false},Tone::Canary}},policy,&value)&&value.runs.size()==1&&value.line.cells==4096);
    CHECK(renderTerminalSegments({{{std::string(4096,'x'),{},false},Tone::Canary}},policy,ColorLevel::TrueColor,&text)&&unpaint(text)==std::string(4096,'x'));
    policy.columns=80;
    for(const auto &pair:std::vector<std::pair<Tone,std::string>>{{Tone::Plain,"x"},{Tone::Dark,"\x1b[90mx\x1b[0m"},{Tone::Rule,"\x1b[90mx\x1b[0m"},
        {Tone::Canary,"\x1b[93mx\x1b[0m"},{Tone::Donation,"\x1b[97mx\x1b[0m"},{Tone::Amber,"\x1b[33mx\x1b[0m"},{Tone::Red,"\x1b[91mx\x1b[0m"},{Tone::Green,"\x1b[92mx\x1b[0m"}}){
        CHECK(renderTerminalSegments({{{"x",{},false},pair.first}},policy,ColorLevel::Basic16,&text)&&text==pair.second);
    }
    CHECK(renderTerminalSegments({{{"x",{},false},Tone::Canary}},policy,ColorLevel::Palette256,&text)&&text=="\x1b[38;5;220mx\x1b[0m");
    CHECK(renderTerminalSegments({{{"x",{},false},Tone::Donation}},policy,ColorLevel::TrueColor,&text)&&text=="\x1b[38;2;255;243;176mx\x1b[0m");
    policy.columns=3;CHECK(renderTerminalSegments({{{"1.23 PRL",{},true},Tone::Canary}},policy,ColorLevel::TrueColor,&text)&&text.empty());
    if(!terminalTextBackend().unicode)return;
    policy.order=TerminalTextOrder::Visual;policy.direction=TerminalTextDirection::RightToLeft;policy.columns=80;
    CHECK(layoutTerminalSegments({{{u8"אב", "ab",false},Tone::Red},{{u8"גד","cd",false},Tone::Green}},policy,&value)&&value.line.text==u8"דגבא");
    CHECK(value.runs.size()==2&&value.runs[0].tone==Tone::Green&&value.runs[1].tone==Tone::Red);
    CHECK(renderTerminalSegments({{{u8"שָׁלוֹם","shalom",false},Tone::Canary}},policy,ColorLevel::Palette256,&text)&&unpaint(text)==u8"םוֹלשָׁ");
    CHECK(layoutTerminalSegments({{{u8"\u0301","?",false},Tone::Amber}},policy,&value)&&value.line.replacedOrphan&&value.runs.size()==1&&value.runs[0].tone==Tone::Amber&&value.runs[0].content.text=="?");
    policy.order=TerminalTextOrder::Logical;
    CHECK(layoutTerminalSegments({{{u8"قيمة ","value ",false},Tone::Green},{{"1.23 PRL",{},true},Tone::Donation}},policy,&value));
    for(const auto &run:value.runs){if(run.content.control)CHECK(run.tone==Tone::Plain&&run.content.cells==0);}
    value.line.text="unchanged";CHECK(!layoutTerminalSegments({{{"e",{},false},Tone::Red},{{u8"\u0301","?",false},Tone::Green}},policy,&value)&&value.line.text=="unchanged");
}
}
int main(){legacyPalette();matrix();focused();return finishTests("typed terminal provenance and fixed-palette color after layout (no ordinary activation)");}
