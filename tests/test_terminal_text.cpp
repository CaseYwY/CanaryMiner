// CPU presentation contracts. ICU algorithm conformance != terminal/font/shaping/assistive-technology qualification.
#include "product/terminal_text.h"
#include "product/localization.h"
#include "product_test_util.h"
#include <fstream>
#include <sstream>
using namespace om;
namespace {
std::string encode(uint32_t cp){std::string out;if(cp<128)out+=static_cast<char>(cp);else if(cp<2048){out+=static_cast<char>(0xc0|(cp>>6));out+=static_cast<char>(0x80|(cp&63));}
    else if(cp<65536){out+=static_cast<char>(0xe0|(cp>>12));out+=static_cast<char>(0x80|((cp>>6)&63));out+=static_cast<char>(0x80|(cp&63));}
    else{out+=static_cast<char>(0xf0|(cp>>18));out+=static_cast<char>(0x80|((cp>>12)&63));out+=static_cast<char>(0x80|((cp>>6)&63));out+=static_cast<char>(0x80|(cp&63));}return out;}
bool same(const TerminalTextLine &a,const TerminalTextLine &b){return a.text==b.text&&a.cells==b.cells&&a.clipped==b.clipped&&a.replacedOrphan==b.replacedOrphan&&a.fallback==b.fallback;}
void asciiAndRefusal(){
    TerminalTextLine line;TerminalTextPolicy p;p.columns=9;
    CHECK(layoutTerminalText({{u8"保存","Save",false},{" ",{},false},{"1.23 PRL",{},true}},p,&line)&&line.text=="Save "&&line.cells==5&&line.clipped&&line.fallback==TerminalTextFallback::RequestedAscii);
    p.columns=20;CHECK(layoutTerminalText({{u8"保存","Save",false},{" ",{},false},{"1.23 PRL",{},true}},p,&line)&&line.text=="Save 1.23 PRL"&&line.cells==13&&!line.clipped);
    p.columns=3;CHECK(layoutTerminalText({{u8"保存","Save",false}},p,&line)&&line.text=="Sav"&&line.cells==3&&line.clipped);
    p.columns=0;CHECK(layoutTerminalText({{"x",{},false}},p,&line)&&line.text.empty()&&!line.cells&&line.clipped);
    CHECK(layoutTerminalText({},p,&line)&&line.text.empty()&&!line.clipped);
    const TerminalTextLine before{"unchanged",7,true,true,TerminalTextFallback::BackendDisabled};
    for(const auto &spans:std::vector<std::vector<TerminalTextSpan>>{
        {{"\x1b[31m","safe",false}},{{"x\ny","safe",false}},{{std::string("x\0y",3),"safe",false}},{{u8"\u200f", "safe",false}},{{u8"\u061c", "safe",false}},
        {{u8"\u202e", "safe",false}},{{u8"\u2066", "safe",false}},{{"\xc0\x80","safe",false}},{{u8"中文",{},false}},{{u8"中文","identity",true}},
        {{"USD","USDT",true}},{{"x","\x1b",false}},{{std::string(4097,'x'),{},false}},{{std::string(4096,'x'),{},false},{"x",{},false}},
        {{"x",std::string(4097,'x'),false}},std::vector<TerminalTextSpan>(65,{"x",{},false})}){
        line=before;CHECK(!layoutTerminalText(spans,p,&line)&&same(line,before));}
    for(unsigned which=0;which<5;++which){p={};if(which==0)p.columns=4097;if(which==1)p.emojiCells=0;if(which==2)p.ambiguousCells=3;
        if(which==3)p.order=static_cast<TerminalTextOrder>(99);
        if(which==4)p.direction=static_cast<TerminalTextDirection>(99);
        line=before;CHECK(!layoutTerminalText({{"safe",{},false}},p,&line)&&same(line,before));}
    p={};CHECK(!layoutTerminalText({},p,nullptr));
    p.columns=4096;CHECK(layoutTerminalText({{std::string(4096,'x'),{},false}},p,&line)&&line.cells==4096&&line.text.size()==4096&&!line.clipped);
    auto backend=terminalTextBackend();p.order=TerminalTextOrder::Logical;
    CHECK(layoutTerminalText({{u8"保存","Save",false}},p,&line));
    CHECK(backend.unicode?(line.text==u8"保存"&&line.cells==4&&line.fallback==TerminalTextFallback::None):(line.text=="Save"&&line.fallback==TerminalTextFallback::BackendDisabled));
}
void unicode(){
    if(!terminalTextBackend().unicode){std::vector<std::size_t> points={9};CHECK(!terminalGraphemeBoundaries(u8"中",&points)&&points==std::vector<std::size_t>{9});return;}
    TerminalTextPolicy p;p.order=TerminalTextOrder::Logical;p.columns=4096;TerminalTextLine line;
    for(const std::string &cluster:std::vector<std::string>{u8"e\u0301",u8"👩‍💻",u8"🇨🇳",u8"👍🏽",u8"1️⃣",u8"क्‍ष",u8"বাংলা"}){
        std::vector<std::size_t> points;CHECK(terminalGraphemeBoundaries(cluster,&points)&&points.front()==0&&points.back()==cluster.size());
        for(std::size_t width=0;width<=8;++width){p.columns=width;CHECK(layoutTerminalText({{cluster,"fallback",false}},p,&line)&&line.cells<=width&&i18n::safeMessageText(line.text));
            CHECK(std::find(points.begin(),points.end(),line.text.size())!=points.end()&&cluster.substr(0,line.text.size())==line.text);}
    }
    for(const auto &item:std::vector<std::pair<std::string,std::size_t>>{{u8"e\u0301",1},{u8"中",2},{u8"👩‍💻",2},{u8"🇨🇳",2},{u8"👍🏽",2},{u8"1️⃣",2},{u8"가",2},{u8"a",1}}){
        p.columns=20;CHECK(layoutTerminalText({{item.first,"fallback",false}},p,&line)&&line.text==item.first&&line.cells==item.second&&!line.clipped);}
    for(unsigned width:{1u,2u}){p.ambiguousCells=width;CHECK(layoutTerminalText({{u8"·",".",false}},p,&line)&&line.cells==width);
        p.emojiCells=width;CHECK(layoutTerminalText({{u8"👩‍💻","emoji",false}},p,&line)&&line.cells==width);}
    p.ambiguousCells=1;p.emojiCells=2;CHECK(layoutTerminalText({{u8"a\u20e3","a",false}},p,&line)&&line.cells==1); // non-emoji combining sequence
    p={};p.order=TerminalTextOrder::Logical;p.columns=1;
    CHECK(layoutTerminalText({{u8"中x","Cx",false}},p,&line)&&line.text.empty()&&line.cells==0&&line.clipped);
    CHECK(layoutTerminalText({{u8"\u0301","?",false}},p,&line)&&line.text=="?"&&line.cells==1&&line.replacedOrphan);
    CHECK(layoutTerminalText({{u8"\u200d","?",false}},p,&line)&&line.text=="?"&&line.replacedOrphan);
    line.text="unchanged";CHECK(!layoutTerminalText({{"e",{},false},{u8"\u0301","?",false}},p,&line)&&line.text=="unchanged");
    p.columns=12;CHECK(layoutTerminalText({{"amount ",{},false},{"1.23 PRL",{},true}},p,&line)&&line.text=="amount "&&line.cells==7&&line.clipped);
    p.columns=80;CHECK(layoutTerminalText({{u8"قيمة ","value ",false},{"1.23 PRL / USD",{},true}},p,&line)&&line.text.find(u8"\u20661.23 PRL / USD\u2069")!=line.text.npos);
    p.direction=TerminalTextDirection::RightToLeft;CHECK(layoutTerminalText({{u8"אבג","abc",false}},p,&line)&&line.text==u8"\u2067אבג\u2069"&&line.cells==3);
    p.direction=TerminalTextDirection::LeftToRight;CHECK(layoutTerminalText({{"abc",{},false}},p,&line)&&line.text==u8"\u2066abc\u2069"&&line.cells==3);
    p.order=TerminalTextOrder::Visual;p.direction=TerminalTextDirection::RightToLeft;
    CHECK(layoutTerminalText({{u8"אבג", "abc",false}},p,&line)&&line.text==u8"גבא"&&line.cells==3);
    CHECK(layoutTerminalText({{u8"שָׁלוֹם","shalom",false}},p,&line)&&line.text==u8"םוֹלשָׁ"&&line.cells==4);
    for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft}){
        p.direction=direction;
        for(auto order:{TerminalTextOrder::Logical,TerminalTextOrder::Visual}){p.order=order;
            CHECK(layoutTerminalText({{u8"قيمة (", "value (",false},{"https://fixture.test:8048 / 1.23 PRL / USD / UTC",{},true},{u8") 👩‍💻 e\u0301", ") emoji e",false}},p,&line)&&
                line.text.find("https://fixture.test:8048 / 1.23 PRL / USD / UTC")!=line.text.npos&&line.text.find(u8"👩‍💻")!=line.text.npos&&line.text.find(u8"e\u0301")!=line.text.npos&&line.cells<=p.columns);
            if(order==TerminalTextOrder::Visual)CHECK(i18n::safeMessageText(line.text)&&line.text.find(u8"\u2066")==line.text.npos);}
    }
    std::vector<std::size_t> points={9};for(const std::string &bad:std::vector<std::string>{"\x80","\xc0\x80","\xed\xa0\x80","\xf4\x90\x80\x80",std::string(4097,'x')}){
        CHECK(!terminalGraphemeBoundaries(bad,&points)&&points==std::vector<std::size_t>{9});}
    CHECK(terminalGraphemeBoundaries("",&points)&&points==std::vector<std::size_t>{0});
    CHECK(terminalGraphemeBoundaries("\r\n",&points)&&points==std::vector<std::size_t>({0,2}));
}
void corpus(const char *path){
    CHECK(terminalTextBackend().unicode);std::ifstream file(path);CHECK(file.good());std::string line;unsigned cases=0;
    while(std::getline(file,line)){line=line.substr(0,line.find('#'));std::istringstream words(line);std::string word,text;std::vector<std::size_t> expected;
        while(words>>word){if(word==u8"÷")expected.push_back(text.size());else if(word!=u8"×"){uint32_t cp=0;std::istringstream hex(word);hex>>std::hex>>cp;CHECK(!hex.fail()&&cp<=0x10ffff);text+=encode(cp);}}
        if(expected.empty())continue;
        std::vector<std::size_t> actual;CHECK(terminalGraphemeBoundaries(text,&actual)&&actual==expected);++cases;
    }
    CHECK(cases>500);std::printf("ICU supplied grapheme corpus: %u cases\n",cases);
}
void catalogMatrix(){
    TerminalTextLine line;const std::string identity="1.23 PRL / USD / UTC";
    for(const auto &pack:i18n::localePacks())for(std::size_t key=0;key<i18n::messageKeyCount;++key){
        const std::vector<TerminalTextSpan> spans={{std::string(pack.messages[key]),std::string(i18n::localePacks()[0].messages[key]),false},{" ",{},false},{identity,{},true}};
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;
            for(std::size_t width:{0u,1u,2u,7u,19u,79u}){policy.columns=width;
                CHECK(layoutTerminalText(spans,policy,&line)&&line.cells<=width&&line.text.size()<=8192);
                // An identity is omitted atomically, never published as a truncated amount, asset or clock label.
                const auto start=line.text.find("1.23");CHECK(start==line.text.npos||line.text.substr(start,identity.size())==identity);
                if(width==79)CHECK(!line.clipped&&start!=line.text.npos);
            }
        }
    }
}
}
int main(int argc,char **argv){if(argc>2)return 2;const auto backend=terminalTextBackend();CHECK(backend.unicode?(backend.implementation=="ICU"&&!backend.version.empty()&&!backend.unicodeVersion.empty()):(backend.implementation=="ASCII"&&backend.version.empty()&&backend.unicodeVersion.empty()));
    std::printf("presentation backend: %s %s / Unicode %s\n",backend.implementation.c_str(),backend.version.c_str(),backend.unicodeVersion.c_str());
    asciiAndRefusal();unicode();catalogMatrix();if(argc==2)corpus(argv[1]);return finishTests("bounded terminal grapheme/cell/bidi presentation (not glyph/terminal qualification)");}
