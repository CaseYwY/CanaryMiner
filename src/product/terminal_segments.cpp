#include "terminal_segments.h"
namespace om {
bool layoutTerminalSegments(const std::vector<TerminalSegment> &spans,const TerminalTextPolicy &policy,ColoredTerminalLine *out){
    if(!out||spans.size()>64)return false;
    std::vector<TerminalTextSpan> bare;for(const auto &span:spans){if(static_cast<unsigned>(span.tone)>static_cast<unsigned>(Tone::Green))return false;bare.push_back(span.content);}
    TerminalTextRunLine layout;if(!layoutTerminalTextRuns(bare,policy,&layout))return false;
    ColoredTerminalLine result;result.line=std::move(layout.line);
    for(auto &run:layout.runs){if(!run.control&&run.sourceSpan>=spans.size())return false;
        const auto tone=run.control?Tone::Plain:spans[run.sourceSpan].tone;result.runs.push_back({std::move(run),tone});}
    *out=std::move(result);return true;
}
bool renderTerminalSegments(const std::vector<TerminalSegment> &spans,const TerminalTextPolicy &policy,ColorLevel color,std::string *out){
    if(!out||static_cast<unsigned>(color)>static_cast<unsigned>(ColorLevel::TrueColor))return false;
    ColoredTerminalLine layout;if(!layoutTerminalSegments(spans,policy,&layout))return false;
    std::string result;for(const auto &run:layout.runs){const auto rendered=dash_detail::paintLiteral(run.content.text,run.tone,color);
        if(rendered.size()>65536-result.size())return false;
        result+=rendered;}
    *out=std::move(result);return true;
}
} // namespace om
