#include "terminal_text.h"
#include "terminal_runs.h"
#include "localization.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <numeric>
#if defined(CANARY_TERMINAL_ICU)
#include <unicode/ubidi.h>
#include <unicode/ubrk.h>
#include <unicode/uchar.h>
#include <unicode/ustring.h>
#include <unicode/uversion.h>
#endif
namespace om {
namespace {
constexpr std::size_t inputLimit=4096,outputLimit=8192;
bool ascii(std::string_view text){return std::all_of(text.begin(),text.end(),[](unsigned char c){return c>=32&&c<=126;});}
bool scalar(std::string_view text,std::size_t &at,uint32_t &cp){
    if(at==text.size())return false;
    const auto first=static_cast<unsigned char>(text[at++]);cp=first;unsigned count=0;
    if(first<0x80)count=0;else if(first>=0xc2&&first<=0xdf){count=1;cp=first&31;}else if(first>=0xe0&&first<=0xef){count=2;cp=first&15;}
    else if(first>=0xf0&&first<=0xf4){count=3;cp=first&7;}else return false;
    if(count>text.size()-at)return false;
    for(unsigned i=0;i<count;++i){const auto next=static_cast<unsigned char>(text[at++]);if((next&0xc0)!=0x80)return false;cp=(cp<<6)|(next&63);}
    return !((count==1&&cp<0x80)||(count==2&&cp<0x800)||(count==3&&cp<0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff));
}
bool renderSafe(std::string_view text){
    if(!i18n::safeMessageText(text))return false;
    for(std::size_t at=0;at<text.size();){uint32_t cp=0;if(!scalar(text,at,cp)||cp==0x061c||cp==0x200e||cp==0x200f)return false;}
    return true;
}
bool atomicLtr(const TerminalTextSpan &span){return span.invariant||span.atom==TerminalTextAtom::LeftToRight;}
bool valid(const std::vector<TerminalTextSpan> &spans,const TerminalTextPolicy &policy){
    if(spans.size()>64||policy.columns>4096||(policy.ambiguousCells!=1&&policy.ambiguousCells!=2)||(policy.emojiCells!=1&&policy.emojiCells!=2)||
        static_cast<unsigned>(policy.order)>2||static_cast<unsigned>(policy.direction)>2)return false;
    std::size_t bytes=0,fallbackBytes=0;
    for(const auto &s:spans){if(s.text.size()>inputLimit-bytes||!renderSafe(s.text)||!ascii(s.ascii)||static_cast<unsigned>(s.atom)>1)return false;bytes+=s.text.size();
        if((s.invariant&&(!ascii(s.text)||(!s.ascii.empty()&&s.ascii!=s.text)))||(!ascii(s.text)&&s.ascii.empty()))return false;
        const auto &fallback=s.ascii.empty()?s.text:s.ascii;if(fallback.size()>inputLimit-fallbackBytes)return false;fallbackBytes+=fallback.size();}
    return true;
}
void appendRun(std::vector<TerminalTextRun> *runs,std::string text,std::size_t span,std::size_t width,bool control=false){
    if(!runs||text.empty())return;
    if(!runs->empty()&&!runs->back().padding&&runs->back().control==control&&(control||runs->back().sourceSpan==span)){runs->back().text+=text;runs->back().cells+=width;}
    else runs->push_back({std::move(text),span,width,control,false});
}
TerminalTextLine asciiLine(const std::vector<TerminalTextSpan> &spans,const TerminalTextPolicy &policy,TerminalTextFallback reason,std::vector<TerminalTextRun> *runs=nullptr){
    TerminalTextLine out;out.fallback=reason;
    for(std::size_t span=0;span<spans.size();++span){const auto &s=spans[span];const auto &text=s.ascii.empty()?s.text:s.ascii;const auto remaining=policy.columns-out.cells;
        if(text.size()>remaining){if(!atomicLtr(s)){out.text.append(text,0,remaining);out.cells+=remaining;appendRun(runs,text.substr(0,remaining),span,remaining);}out.clipped=true;break;}
        out.text+=text;out.cells+=text.size();appendRun(runs,text,span,text.size());}
    return out;
}
#if defined(CANARY_TERMINAL_ICU)
using Utf16=std::vector<UChar>;
bool toUtf16(std::string_view text,Utf16 &out,std::vector<std::size_t> *offsets=nullptr){
    if(text.size()>inputLimit)return false;
    Utf16 result;std::vector<std::size_t> map;
    for(std::size_t at=0;at<text.size();){const auto begin=at;uint32_t cp=0;if(!scalar(text,at,cp))return false;
        map.push_back(begin);if(cp<=0xffff)result.push_back(static_cast<UChar>(cp));else{cp-=0x10000;result.push_back(static_cast<UChar>(0xd800+(cp>>10)));map.push_back(begin);result.push_back(static_cast<UChar>(0xdc00+(cp&1023)));}}
    map.push_back(text.size());out=std::move(result);if(offsets)*offsets=std::move(map);return true;
}
bool utf8(const UChar *text,std::size_t length,std::string &out){
    if(length>outputLimit)return false;
    std::string result(outputLimit,'\0');int32_t written=0;UErrorCode error=U_ZERO_ERROR;
    u_strToUTF8(result.data(),static_cast<int32_t>(result.size()),&written,text,static_cast<int32_t>(length),&error);
    if(U_FAILURE(error)||written<0||static_cast<std::size_t>(written)>outputLimit)return false;
    result.resize(static_cast<std::size_t>(written));out=std::move(result);return true;
}
bool boundaries(const Utf16 &text,std::vector<int32_t> &out,UBreakIteratorType type=UBRK_CHARACTER){
    UErrorCode error=U_ZERO_ERROR;
    const UChar empty=0;std::unique_ptr<UBreakIterator,decltype(&ubrk_close)> iterator(ubrk_open(type,"root",text.empty()?&empty:text.data(),static_cast<int32_t>(text.size()),&error),ubrk_close);
    if(U_FAILURE(error)||!iterator)return false;
    std::vector<int32_t> result;for(int32_t at=ubrk_first(iterator.get());at!=UBRK_DONE;at=ubrk_next(iterator.get()))result.push_back(at);
    if(result.empty()||result.front()!=0||result.back()!=static_cast<int32_t>(text.size()))return false;
    out=std::move(result);return true;
}
std::size_t cells(const Utf16 &text,int32_t begin,int32_t end,const TerminalTextPolicy &policy){
    std::size_t width=0;
    for(int32_t at=begin;at<end;){UChar32 cp;U16_NEXT(text.data(),at,end,cp);const auto category=u_charType(cp);
        const auto east=u_getIntPropertyValue(cp,UCHAR_EAST_ASIAN_WIDTH);
        if(category!=U_NON_SPACING_MARK&&category!=U_ENCLOSING_MARK&&category!=U_COMBINING_SPACING_MARK&&category!=U_FORMAT_CHAR&&
            !u_hasBinaryProperty(cp,UCHAR_DEFAULT_IGNORABLE_CODE_POINT))width=std::max(width,static_cast<std::size_t>(east==U_EA_WIDE||east==U_EA_FULLWIDTH?2:east==U_EA_AMBIGUOUS?policy.ambiguousCells:1));
    }
    // Use maintained UTS #51 string membership, not a homemade ZWJ/keycap recognizer.
    if(u_stringHasBinaryProperty(text.data()+begin,end-begin,UCHAR_RGI_EMOJI))return policy.emojiCells;
    return width;
}
struct Cluster {int32_t begin=0,end=0;std::size_t span=0,width=0;bool orphan=false;};
bool makeClusters(const std::vector<TerminalTextSpan> &spans,const TerminalTextPolicy &policy,Utf16 &text,std::vector<Cluster> &clusters){
    std::vector<int32_t> spanEnds;for(const auto &span:spans){Utf16 part;if(!toUtf16(span.text,part))return false;text.insert(text.end(),part.begin(),part.end());spanEnds.push_back(static_cast<int32_t>(text.size()));}
    std::vector<int32_t> breaks;if(!boundaries(text,breaks))return false;
    for(auto end:spanEnds)if(!std::binary_search(breaks.begin(),breaks.end(),end))return false; // never split one cluster between tones/identity spans
    std::size_t span=0;
    for(std::size_t i=1;i<breaks.size();++i){while(span<spanEnds.size()&&breaks[i-1]>=spanEnds[span])++span;if(span==spans.size())return false;
        const auto width=cells(text,breaks[i-1],breaks[i],policy);clusters.push_back({breaks[i-1],breaks[i],span,width?width:1,width==0});}
    return true;
}
bool unicodeLine(const std::vector<TerminalTextSpan> &spans,const TerminalTextPolicy &policy,TerminalTextLine &out,std::vector<TerminalTextRun> *runs=nullptr){
    Utf16 text;std::vector<Cluster> clusters;if(!makeClusters(spans,policy,text,clusters))return false;
    TerminalTextLine result;std::size_t retained=0;
    while(retained<clusters.size()){
        const auto current=clusters[retained].span;std::size_t end=retained+1,width=clusters[retained].width;
        if(atomicLtr(spans[current]))while(end<clusters.size()&&clusters[end].span==current)width+=clusters[end++].width;
        if(width>policy.columns-result.cells){result.clipped=true;break;}
        result.cells+=width;for(auto i=retained;i<end;++i)result.replacedOrphan=result.replacedOrphan||clusters[i].orphan;retained=end;
    }
    clusters.resize(retained);if(clusters.empty()){out=std::move(result);return true;}
    Utf16 decorated;std::vector<int32_t> starts;std::size_t previous=spans.size();
    for(const auto &cluster:clusters){if(previous!=cluster.span){if(previous!=spans.size()&&atomicLtr(spans[previous])){decorated.push_back(0x2069);if(policy.order==TerminalTextOrder::Logical)appendRun(runs,u8"\u2069",0,0,true);}
            if(atomicLtr(spans[cluster.span])){decorated.push_back(0x2066);if(policy.order==TerminalTextOrder::Logical)appendRun(runs,u8"\u2066",0,0,true);}
            previous=cluster.span;}
        starts.push_back(static_cast<int32_t>(decorated.size()));
        if(cluster.orphan)decorated.push_back('?');else decorated.insert(decorated.end(),text.begin()+cluster.begin,text.begin()+cluster.end);
        if(runs&&policy.order==TerminalTextOrder::Logical){std::string part="?";if(!cluster.orphan&&!utf8(text.data()+cluster.begin,static_cast<std::size_t>(cluster.end-cluster.begin),part))return false;
            appendRun(runs,std::move(part),cluster.span,cluster.width);}
    }
    if(atomicLtr(spans[previous])){decorated.push_back(0x2069);if(policy.order==TerminalTextOrder::Logical)appendRun(runs,u8"\u2069",0,0,true);}
    if(policy.order==TerminalTextOrder::Logical){
        if(policy.direction!=TerminalTextDirection::Auto){decorated.insert(decorated.begin(),policy.direction==TerminalTextDirection::RightToLeft?0x2067:0x2066);decorated.push_back(0x2069);
            if(runs){runs->insert(runs->begin(),{policy.direction==TerminalTextDirection::RightToLeft?u8"\u2067":u8"\u2066",0,0,true,false});appendRun(runs,u8"\u2069",0,0,true);}}
        if(!utf8(decorated.data(),decorated.size(),result.text))return false;
        out=std::move(result);return true;}
    UErrorCode error=U_ZERO_ERROR;std::unique_ptr<UBiDi,decltype(&ubidi_close)> bidi(ubidi_openSized(static_cast<int32_t>(decorated.size()),0,&error),ubidi_close);
    if(U_FAILURE(error)||!bidi)return false;
    const UBiDiLevel level=policy.direction==TerminalTextDirection::Auto?UBIDI_DEFAULT_LTR:policy.direction==TerminalTextDirection::RightToLeft?1:0;
    ubidi_setPara(bidi.get(),decorated.data(),static_cast<int32_t>(decorated.size()),level,nullptr,&error);if(U_FAILURE(error))return false;
    std::vector<int32_t> visual(decorated.size());ubidi_getVisualMap(bidi.get(),visual.data(),&error);if(U_FAILURE(error))return false;
    std::vector<int32_t> logical(decorated.size());for(std::size_t i=0;i<visual.size();++i){if(visual[i]<0||static_cast<std::size_t>(visual[i])>=logical.size())return false;logical[visual[i]]=static_cast<int32_t>(i);}
    std::vector<std::pair<int32_t,std::size_t>> order;
    for(std::size_t i=0;i<clusters.size();++i){const auto count=clusters[i].orphan?1:clusters[i].end-clusters[i].begin;
        const auto range=std::minmax_element(logical.begin()+starts[i],logical.begin()+starts[i]+count);
        if(*range.second-*range.first!=count-1)return false; // a directionally discontinuous cluster is never torn apart
        order.emplace_back(*range.first,i);}
    std::sort(order.begin(),order.end());Utf16 finalText;
    for(const auto &item:order){const auto i=item.second;const auto &cluster=clusters[i];if(cluster.orphan){finalText.push_back('?');appendRun(runs,"?",cluster.span,cluster.width);continue;}
        const auto begin=finalText.size();
        for(int32_t at=cluster.begin;at<cluster.end;){UChar32 cp;U16_NEXT(text.data(),at,cluster.end,cp);
            if(ubidi_getLevelAt(bidi.get(),starts[i])&1)cp=u_charMirror(cp);
            if(cp<=0xffff)finalText.push_back(static_cast<UChar>(cp));else{finalText.push_back(U16_LEAD(cp));finalText.push_back(U16_TRAIL(cp));}}
        if(runs){std::string part;if(!utf8(finalText.data()+begin,finalText.size()-begin,part))return false;appendRun(runs,std::move(part),cluster.span,cluster.width);}
    }
    if(!utf8(finalText.data(),finalText.size(),result.text))return false;
    out=std::move(result);return true;
}
#endif
}
TerminalTextBackend terminalTextBackend(){
#if defined(CANARY_TERMINAL_ICU)
    UVersionInfo version{},unicode{};u_getVersion(version);u_getUnicodeVersion(unicode);char v[U_MAX_VERSION_STRING_LENGTH]{},u[U_MAX_VERSION_STRING_LENGTH]{};
    u_versionToString(version,v);u_versionToString(unicode,u);return {true,"ICU",v,u};
#else
    return {false,"ASCII",{}, {}};
#endif
}
bool terminalGraphemeBoundaries(std::string_view text,std::vector<std::size_t> *out){
    if(!out||text.size()>inputLimit)return false;
#if defined(CANARY_TERMINAL_ICU)
    Utf16 utf16;std::vector<std::size_t> offsets;if(!toUtf16(text,utf16,&offsets))return false;
    std::vector<int32_t> points;if(!boundaries(utf16,points))return false;
    std::vector<std::size_t> result;for(auto point:points)result.push_back(offsets[point]);*out=std::move(result);return true;
#else
    if(!ascii(text))return false;
    std::vector<std::size_t> result(text.size()+1);std::iota(result.begin(),result.end(),0);*out=std::move(result);return true;
#endif
}
bool layoutTerminalText(const std::vector<TerminalTextSpan> &spans,const TerminalTextPolicy &policy,TerminalTextLine *out){
    if(!out||!valid(spans,policy))return false;
    if(policy.order==TerminalTextOrder::Ascii){*out=asciiLine(spans,policy,TerminalTextFallback::RequestedAscii);return true;}
#if defined(CANARY_TERMINAL_ICU)
    return unicodeLine(spans,policy,*out);
#else
    *out=asciiLine(spans,policy,TerminalTextFallback::BackendDisabled);return true;
#endif
}
namespace {
bool completeRunLine(const TerminalTextRunLine &result,std::size_t spans,const TerminalTextPolicy &policy,bool padding=false){
    if(result.runs.size()>(padding?4231u:4226u))return false;
    std::string joined;std::size_t width=0;
    for(const auto &run:result.runs){if(run.text.size()>outputLimit-joined.size()||run.cells>policy.columns-width||
            (!run.control&&!run.padding&&run.sourceSpan>=spans)||(run.control&&(run.cells||run.padding))||
            (run.padding&&(!padding||run.cells!=run.text.size()||run.text.find_first_not_of(' ')!=run.text.npos)))return false;
        joined+=run.text;width+=run.cells;}
    return joined==result.line.text&&width==result.line.cells;
}
}
bool layoutTerminalTextRuns(const std::vector<TerminalTextSpan> &spans,const TerminalTextPolicy &policy,TerminalTextRunLine *out){
    if(!out||!valid(spans,policy))return false;
    TerminalTextRunLine result;
    if(policy.order==TerminalTextOrder::Ascii)result.line=asciiLine(spans,policy,TerminalTextFallback::RequestedAscii,&result.runs);
#if defined(CANARY_TERMINAL_ICU)
    else if(!unicodeLine(spans,policy,result.line,&result.runs))return false;
#else
    else result.line=asciiLine(spans,policy,TerminalTextFallback::BackendDisabled,&result.runs);
#endif
    if(!completeRunLine(result,spans.size(),policy))return false;
    *out=std::move(result);return true;
}
namespace {
bool wrapRows(const std::vector<TerminalTextSpan> &spans,const TerminalTextPolicy &policy,std::size_t maximumLines,bool provenance,std::vector<TerminalTextRunLine> *out){
    if(!out||!valid(spans,policy)||!policy.columns||!maximumLines||maximumLines>4096)return false;
    struct Unit {std::string text;std::size_t span,width;bool opportunity;};std::vector<Unit> units;
    const auto fallback=policy.order==TerminalTextOrder::Ascii?TerminalTextFallback::RequestedAscii:TerminalTextFallback::BackendDisabled;
#if defined(CANARY_TERMINAL_ICU)
    const bool unicode=policy.order!=TerminalTextOrder::Ascii;
    if(unicode){Utf16 text;std::vector<Cluster> clusters;std::vector<int32_t> opportunities;
        if(!makeClusters(spans,policy,text,clusters)||!boundaries(text,opportunities,UBRK_LINE))return false;
        for(std::size_t i=0;i<clusters.size();){const auto start=i,span=clusters[i].span;std::size_t width=clusters[i].width;
            if(atomicLtr(spans[span]))while(i+1<clusters.size()&&clusters[i+1].span==span)width+=clusters[++i].width;
            const auto begin=clusters[start].begin,end=clusters[i].end;std::string value;if(!utf8(text.data()+begin,static_cast<std::size_t>(end-begin),value))return false;
            units.push_back({std::move(value),span,width,std::binary_search(opportunities.begin(),opportunities.end(),end)});++i;}
    }else
#endif
    {for(std::size_t span=0;span<spans.size();++span){const auto &s=spans[span];const auto &text=s.ascii.empty()?s.text:s.ascii;
        if(atomicLtr(s)){if(!text.empty())units.push_back({text,span,text.size(),true});}
        else for(char ch:text)units.push_back({std::string(1,ch),span,1,ch==' '});}}
    std::vector<TerminalTextRunLine> result;std::size_t cursor=0,total=0;
    do{if(result.size()==maximumLines)return false;
        std::size_t end=cursor,width=0,last=cursor;
        while(end<units.size()&&units[end].width<=policy.columns-width){width+=units[end].width;if(units[end].opportunity)last=end+1;++end;}
        if(end==cursor&&cursor<units.size())return false;
        if(end<units.size()&&last>cursor)end=last;
        std::vector<TerminalTextSpan> parts;std::vector<std::size_t> sources;std::size_t previous=spans.size();
        for(auto i=cursor;i<end;++i){const auto &unit=units[i];if(unit.span!=previous){parts.emplace_back(unit.text,std::string{},spans[unit.span].invariant,spans[unit.span].atom);sources.push_back(unit.span);previous=unit.span;}
            else parts.back().text+=unit.text;}
        TerminalTextRunLine row;auto &line=row.line;auto *runs=provenance?&row.runs:nullptr;
#if defined(CANARY_TERMINAL_ICU)
        if(unicode){if(!unicodeLine(parts,policy,line,runs)||line.clipped)return false;}else
#endif
        line=asciiLine(parts,policy,fallback,runs);
        if(provenance){if(!completeRunLine(row,parts.size(),policy))return false;
            for(auto &run:row.runs)if(!run.control)run.sourceSpan=sources[run.sourceSpan];
            if(!completeRunLine(row,spans.size(),policy))return false;}
        if(line.text.size()>32768-total)return false;
        total+=line.text.size();result.push_back(std::move(row));cursor=end;
    }while(cursor<units.size());
    *out=std::move(result);return true;
}
}
bool wrapTerminalText(const std::vector<TerminalTextSpan> &spans,const TerminalTextPolicy &policy,std::size_t maximumLines,std::vector<TerminalTextLine> *out){
    if(!out)return false;
    std::vector<TerminalTextRunLine> rows;if(!wrapRows(spans,policy,maximumLines,false,&rows))return false;
    std::vector<TerminalTextLine> result;for(auto &row:rows)result.push_back(std::move(row.line));*out=std::move(result);return true;
}
bool wrapTerminalTextRuns(const std::vector<TerminalTextSpan> &spans,const TerminalTextPolicy &policy,std::size_t maximumLines,std::vector<TerminalTextRunLine> *out){
    return out&&wrapRows(spans,policy,maximumLines,true,out);
}
bool padTerminalText(const std::vector<TerminalTextSpan> &spans,const TerminalTextPolicy &policy,TerminalTextAlignment alignment,TerminalTextLine *out){
    if(!out||static_cast<unsigned>(alignment)>1)return false;
    TerminalTextLine line;if(!layoutTerminalText(spans,policy,&line))return false;
    const std::string padding(policy.columns-line.cells,' ');
    if(line.fallback==TerminalTextFallback::None&&policy.order==TerminalTextOrder::Logical&&!line.text.empty())line.text=u8"\u2068"+line.text+u8"\u2069";
    line.text=alignment==TerminalTextAlignment::Right?padding+line.text:line.text+padding;
    if(line.fallback==TerminalTextFallback::None&&policy.order==TerminalTextOrder::Logical&&!line.text.empty())line.text=u8"\u2066"+line.text+u8"\u2069";
    if(line.text.size()>outputLimit)return false;
    line.cells=policy.columns;*out=std::move(line);return true;
}
bool padTerminalTextRuns(const std::vector<TerminalTextSpan> &spans,const TerminalTextPolicy &policy,TerminalTextAlignment alignment,TerminalTextRunLine *out){
    if(!out||static_cast<unsigned>(alignment)>1)return false;
    TerminalTextRunLine result;if(!layoutTerminalTextRuns(spans,policy,&result))return false;
    auto &line=result.line;const std::string padding(policy.columns-line.cells,' ');
    const bool logical=line.fallback==TerminalTextFallback::None&&policy.order==TerminalTextOrder::Logical;
    if(logical&&!line.text.empty()){line.text=u8"\u2068"+line.text+u8"\u2069";result.runs.insert(result.runs.begin(),{u8"\u2068",0,0,true,false});appendRun(&result.runs,u8"\u2069",0,0,true);}
    if(!padding.empty()){TerminalTextRun run{padding,0,padding.size(),false,true};if(alignment==TerminalTextAlignment::Right)result.runs.insert(result.runs.begin(),std::move(run));else result.runs.push_back(std::move(run));}
    line.text=alignment==TerminalTextAlignment::Right?padding+line.text:line.text+padding;
    if(logical&&!line.text.empty()){line.text=u8"\u2066"+line.text+u8"\u2069";result.runs.insert(result.runs.begin(),{u8"\u2066",0,0,true,false});appendRun(&result.runs,u8"\u2069",0,0,true);}
    line.cells=policy.columns;if(!completeRunLine(result,spans.size(),policy,true))return false;
    *out=std::move(result);return true;
}
} // namespace om
