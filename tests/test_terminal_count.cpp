// Complete count-domain CPU fixtures; not live workers, attributable credit, financial or terminal qualification.
#include "product/terminal_count.h"
#include "product_test_util.h"
#include <clocale>
using namespace om;
namespace {
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
std::string unpaint(const std::string &text){std::string result;for(std::size_t i=0;i<text.size();){if(text[i]!='\x1b'){result+=text[i++];continue;}
    CHECK(i+1<text.size()&&text[i+1]=='[');i+=2;while(i<text.size()&&((text[i]>='0'&&text[i]<='9')||text[i]==';'))++i;CHECK(i<text.size()&&text[i]=='m');if(i<text.size())++i;}return result;}
std::string substitute(std::string_view pattern,const std::string &count){const auto at=pattern.find("{count}");CHECK(at!=pattern.npos);if(at==pattern.npos)return {};
    CHECK(pattern.find("{count}",at+7)==pattern.npos);return std::string(pattern.substr(0,at))+count+std::string(pattern.substr(at+7));}
void catalogs(){
    const auto &packs=i18n::countLocalePacks();CHECK(packs.size()==i18n::localePacks().size());
    for(std::size_t p=0;p<packs.size();++p){CHECK(packs[p].id==i18n::localePacks()[p].id&&packs[p].rtl==i18n::localePacks()[p].rtl&&packs[p].review==i18n::localePacks()[p].review);
        for(std::size_t key=0;key<2;++key)for(const auto &text:packs[p].messages[key]){std::vector<std::string_view> names;
            CHECK(i18n::templateParameters(text,&names)&&names==std::vector<std::string_view>{"count"});
            CHECK(i18n::validTemplateTranslation(packs[0].messages[key][5],text)&&i18n::safeMessageText(text));}
    }
    std::size_t index=0;for(const auto *category:{"zero","one","two","few","many","other"}){CHECK(i18n::countCategoryIndex(category)==index);++index;}
    CHECK(!i18n::countCategoryIndex("")&&!i18n::countCategoryIndex("Other")&&!i18n::countCategoryIndex("unavailable"));
    CHECK(i18n::countMessageKeyName(i18n::CountMessageKey::Workers)=="counts.workers"&&i18n::countMessageKeyName(i18n::CountMessageKey::AcceptedShares)=="counts.accepted_shares");
    CHECK(i18n::countMessageKeyName(i18n::CountMessageKey::Count).empty()&&i18n::neutralCountTemplate(i18n::CountMessageKey::Count).empty());
}
void provenance(const TerminalMessageLine &message,const std::string &expectedCount,bool complete){
    std::string joined,count;std::size_t cells=0;
    for(const auto &run:message.line.runs){joined+=run.content.text;cells+=run.content.cells;
        if(run.content.control)CHECK(run.tone==Tone::Plain&&!run.content.cells&&!run.content.padding);
        else{CHECK(!run.content.padding&&run.content.sourceSpan<message.metadata.sources.size());const auto &source=message.metadata.sources[run.content.sourceSpan];
            CHECK(source.empty()||source=="count");CHECK(run.tone==(source=="count"?Tone::Canary:Tone::Green));if(source=="count")count+=run.content.text;}
    }
    CHECK(joined==message.line.line.text&&cells==message.line.line.cells);
    CHECK(count==expectedCount||(!complete&&count.empty()));
}
void matrix(){
    for(const auto &format:presentationFormattingLocales())for(const auto &pack:i18n::countLocalePacks())for(auto digits:{PresentationDigits::Latin,PresentationDigits::Locale})
        for(const auto *count:{"0","1","2","3","11","21","100","9007199254740993"})for(std::size_t key=0;key<2;++key){
            TerminalCountRequest request;request.count=count;request.key=static_cast<i18n::CountMessageKey>(key);request.language=std::string(pack.id);request.formattingLocale=format;request.digits=digits;request.literalTone=Tone::Green;
            TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=512;TerminalCountLine line;
            CHECK(layoutTerminalCount(request,policy,&line)&&line.number.ascii==count&&!line.number.rounded&&!line.message.line.line.clipped);
            CHECK(line.number.formattingLocale==format&&line.number.language==pack.id&&!line.number.localeFallback&&!line.number.languageFallback&&!line.number.asciiRecovery);
            CHECK(line.message.metadata.requestedReview==pack.review&&!line.message.metadata.languageFallback&&!line.message.metadata.asciiRecovery);
            CHECK(line.neutral==!terminalTextBackend().unicode&&line.number.pluralAvailable==!line.neutral&&line.number.backendFallback==line.neutral&&line.message.metadata.backendFallback==line.neutral);
            const auto category=i18n::countCategoryIndex(line.number.pluralCategory);CHECK(line.neutral?!category:category.has_value());
            const auto pattern=line.neutral?i18n::neutralCountTemplate(request.key):pack.messages[key][*category];
            CHECK(unisolate(line.message.line.line.text)==substitute(pattern,line.number.text));
            CHECK(line.message.metadata.renderedLanguage==(line.neutral?"en":pack.id)&&line.message.metadata.renderedReview==(line.neutral?i18n::ReviewStatus::Source:pack.review));
            provenance(line.message,line.number.text,true);
        }
}
void policies(){
    for(const auto &pack:i18n::countLocalePacks())for(auto key:{i18n::CountMessageKey::Workers,i18n::CountMessageKey::AcceptedShares})
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft}){
            TerminalCountRequest request;request.language=std::string(pack.id);request.formattingLocale="ar-EG";request.digits=PresentationDigits::Locale;request.count="1234567";request.key=key;request.literalTone=Tone::Green;
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=256;TerminalCountLine line;
            CHECK(layoutTerminalCount(request,policy,&line));const bool ascii=order==TerminalTextOrder::Ascii,neutral=ascii||!terminalTextBackend().unicode;
            CHECK(line.neutral==neutral&&line.number.asciiRecovery==ascii&&line.number.backendFallback==(!ascii&&!terminalTextBackend().unicode));
            CHECK(line.number.pluralAvailable==!neutral&&(neutral?line.number.pluralCategory.empty():!line.number.pluralCategory.empty()));
            provenance(line.message,line.number.text,true);
            if(neutral)CHECK(unisolate(line.message.line.line.text)==substitute(i18n::neutralCountTemplate(key),request.count)&&!line.message.metadata.rtl&&line.message.metadata.renderedLanguage=="en");
            for(auto color:{ColorLevel::None,ColorLevel::Basic16,ColorLevel::Palette256,ColorLevel::TrueColor}){PaintedTerminalCountLine painted;
                CHECK(renderTerminalCount(request,policy,color,&painted)&&unpaint(painted.message.text)==line.message.line.line.text&&painted.number.text==line.number.text&&painted.neutral==neutral);}
            for(std::size_t columns:{0u,1u,6u,19u}){policy.columns=columns;CHECK(layoutTerminalCount(request,policy,&line));provenance(line.message,line.number.text,false);}
            policy.columns=19;TerminalCountRows rows;PaintedTerminalCountRows painted;
            CHECK(wrapTerminalCount(request,policy,64,&rows)&&renderWrappedTerminalCount(request,policy,64,ColorLevel::TrueColor,&painted)&&rows.message.rows.size()==painted.message.rows.size());
            std::string numeric;
            for(std::size_t i=0;i<rows.message.rows.size();++i){const auto &row=rows.message.rows[i];CHECK(unpaint(painted.message.rows[i])==row.line.text&&!row.line.clipped);
                for(const auto &run:row.runs)if(!run.content.control&&!run.content.padding&&rows.message.metadata.sources[run.content.sourceSpan]=="count")numeric+=run.content.text;}
            CHECK(numeric==rows.number.text&&rows.number.ascii==request.count);
        }
}
void branches(){
    if(!terminalTextBackend().unicode)return;
    const std::vector<std::pair<std::string,std::vector<std::pair<std::string,std::string>>>> cases={
        {"ar",{{"0","zero"},{"1","one"},{"2","two"},{"3","few"},{"11","many"},{"100","other"}}},
        {"ru",{{"0","many"},{"1","one"},{"2","few"},{"3","few"},{"11","many"},{"21","one"},{"100","many"},{"9007199254740991","one"},{"9007199254740992","few"},{"9007199254740995","many"}}},
        {"fr",{{"0","one"},{"1","one"},{"2","other"}}},{"en",{{"0","other"},{"1","one"},{"2","other"}}}};
    TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=256;
    for(const auto &language:cases)for(const auto &sample:language.second){TerminalCountRequest request;request.language=language.first;request.count=sample.first;request.grouping=false;request.key=i18n::CountMessageKey::AcceptedShares;TerminalCountLine result;
        CHECK(layoutTerminalCount(request,policy,&result)&&!result.neutral&&result.number.pluralCategory==sample.second);}
    TerminalCountRequest request;request.language="ar";request.count="2";request.grouping=false;TerminalCountLine result;
    CHECK(layoutTerminalCount(request,policy,&result)&&unisolate(result.message.line.line.text)==u8"عاملان (2)");
    request.language="ru";request.count="21";CHECK(layoutTerminalCount(request,policy,&result)&&unisolate(result.message.line.line.text)==u8"21 рабочий процесс");
}
void boundaries(){
    TerminalCountRequest request;request.count="0";TerminalTextPolicy policy;TerminalCountLine result;
    CHECK(layoutTerminalCount(request,policy,&result)&&result.neutral&&result.message.line.line.text=="Workers: 0"&&!result.number.pluralAvailable&&result.number.pluralCategory.empty());
    for(const std::string &count:std::vector<std::string>{"1","9007199254740993","18446744073709551615",std::string(96,'9')}){request.count=count;policy.columns=256;
        CHECK(layoutTerminalCount(request,policy,&result)&&result.number.ascii==count&&!result.number.rounded);
        policy.order=TerminalTextOrder::Logical;CHECK(layoutTerminalCount(request,policy,&result)&&result.number.ascii==count&&!result.number.rounded);policy.order=TerminalTextOrder::Ascii;}
    request.count="1";request.language="en-US";request.formattingLocale="zz-ZZ";policy.order=TerminalTextOrder::Logical;
    CHECK(layoutTerminalCount(request,policy,&result)&&result.message.metadata.languageFallback&&!result.message.metadata.requestedReview&&result.message.metadata.renderedLanguage=="en"&&result.number.languageFallback&&result.number.localeFallback);
    CHECK(unisolate(result.message.line.line.text)==(terminalTextBackend().unicode?"1 worker":"Workers: 1"));
    TerminalCountLine before;before.message.line.line.text="unchanged";before.message.metadata.renderedLanguage="previous";before.number.ascii="sentinel";before.neutral=true;
    for(const std::string &bad:std::vector<std::string>{"","-1","-0","+1","01","00","1.0","1e3","NaN","1 2"," 1",u8"١",std::string("1\0",2),std::string(97,'1')}){
        request.count=bad;result=before;CHECK(!layoutTerminalCount(request,policy,&result)&&result.message.line.line.text=="unchanged"&&result.number.ascii=="sentinel"&&result.neutral&&result.message.metadata.renderedLanguage=="previous");}
    request.count="1";
    for(unsigned mode=0;mode<10;++mode){auto bad=request;auto invalid=policy;
        if(mode==0)bad.key=static_cast<i18n::CountMessageKey>(99);
        if(mode==1)bad.language="en@private";
        if(mode==2)bad.formattingLocale="en@private";
        if(mode==3)bad.digits=static_cast<PresentationDigits>(99);
        if(mode==4)bad.countTone=static_cast<Tone>(99);
        if(mode==5)bad.literalTone=static_cast<Tone>(99);
        if(mode==6)invalid.order=static_cast<TerminalTextOrder>(99);
        if(mode==7)invalid.direction=static_cast<TerminalTextDirection>(99);
        if(mode==8)invalid.ambiguousCells=0;
        if(mode==9)invalid.columns=4097;
        result=before;CHECK(!layoutTerminalCount(bad,invalid,&result)&&result.number.ascii=="sentinel"&&result.message.line.line.text=="unchanged");}
    TerminalCountRows rows;rows.message.metadata.renderedLanguage="previous";rows.number.ascii="sentinel";rows.message.rows.resize(1);rows.message.rows[0].line.text="unchanged";
    policy.order=TerminalTextOrder::Ascii;policy.columns=3;request.count="123456";
    CHECK(!wrapTerminalCount(request,policy,64,&rows)&&rows.message.rows[0].line.text=="unchanged"&&rows.number.ascii=="sentinel"&&rows.message.metadata.renderedLanguage=="previous");
    CHECK(!wrapTerminalCount(request,policy,0,&rows)&&!wrapTerminalCount(request,policy,4097,&rows));
    PaintedTerminalCountRows painted;painted.message.rows={"unchanged"};painted.number.ascii="sentinel";
    CHECK(!renderWrappedTerminalCount(request,policy,64,ColorLevel::None,&painted)&&painted.message.rows==std::vector<std::string>{"unchanged"}&&painted.number.ascii=="sentinel");
    PaintedTerminalCountLine line;line.message.text="unchanged";line.number.ascii="sentinel";
    CHECK(!renderTerminalCount(request,policy,static_cast<ColorLevel>(99),&line)&&line.message.text=="unchanged"&&line.number.ascii=="sentinel");
    CHECK(!renderWrappedTerminalCount(request,policy,64,static_cast<ColorLevel>(99),&painted)&&painted.number.ascii=="sentinel");
    CHECK(!layoutTerminalCount({},policy,nullptr)&&!wrapTerminalCount({},policy,1,nullptr)&&!renderTerminalCount({},policy,ColorLevel::None,nullptr)&&!renderWrappedTerminalCount({},policy,1,ColorLevel::None,nullptr));
}
}
int main(){const std::string locale=std::setlocale(LC_ALL,nullptr);catalogs();matrix();policies();branches();boundaries();CHECK(locale==std::setlocale(LC_ALL,nullptr));return finishTests("complete maintained count templates and honest neutral English recovery (not native activation)");}
