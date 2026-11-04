#include "terminal_segments.h"
namespace om {
namespace {
bool toneValid(Tone tone){return static_cast<unsigned>(tone)<=static_cast<unsigned>(Tone::Green);}
bool inputs(const std::vector<TerminalSegment> &spans,std::vector<TerminalTextSpan> &bare){
    if(spans.size()>64)return false;
    for(const auto &span:spans){if(!toneValid(span.tone))return false;bare.push_back(span.content);}return true;
}
bool attachTones(TerminalTextRunLine layout,const std::vector<TerminalSegment> &spans,Tone padding,ColoredTerminalLine *out){
    ColoredTerminalLine result;result.line=std::move(layout.line);
    for(auto &run:layout.runs){if(!run.control&&!run.padding&&run.sourceSpan>=spans.size())return false;
        const auto tone=run.control?Tone::Plain:run.padding?padding:spans[run.sourceSpan].tone;result.runs.push_back({std::move(run),tone});}
    *out=std::move(result);return true;
}
bool paintLayout(const ColoredTerminalLine &layout,ColorLevel color,std::string *out){
    std::string result;for(const auto &run:layout.runs){const auto rendered=dash_detail::paintLiteral(run.content.text,run.tone,color);
        if(rendered.size()>65536-result.size())return false;
        result+=rendered;}
    *out=std::move(result);return true;
}
bool colorValid(ColorLevel color){return static_cast<unsigned>(color)<=static_cast<unsigned>(ColorLevel::TrueColor);}
}
bool layoutTerminalSegments(const std::vector<TerminalSegment> &spans,const TerminalTextPolicy &policy,ColoredTerminalLine *out){
    std::vector<TerminalTextSpan> bare;if(!out||!inputs(spans,bare))return false;
    TerminalTextRunLine layout;return layoutTerminalTextRuns(bare,policy,&layout)&&attachTones(std::move(layout),spans,Tone::Plain,out);
}
bool renderTerminalSegments(const std::vector<TerminalSegment> &spans,const TerminalTextPolicy &policy,ColorLevel color,std::string *out){
    if(!out||!colorValid(color))return false;
    ColoredTerminalLine layout;return layoutTerminalSegments(spans,policy,&layout)&&paintLayout(layout,color,out);
}
bool wrapTerminalSegments(const std::vector<TerminalSegment> &spans,const TerminalTextPolicy &policy,std::size_t maximumLines,std::vector<ColoredTerminalLine> *out){
    std::vector<TerminalTextSpan> bare;if(!out||!inputs(spans,bare))return false;
    std::vector<TerminalTextRunLine> rows;if(!wrapTerminalTextRuns(bare,policy,maximumLines,&rows))return false;
    std::vector<ColoredTerminalLine> result;for(auto &row:rows){ColoredTerminalLine line;if(!attachTones(std::move(row),spans,Tone::Plain,&line))return false;result.push_back(std::move(line));}
    *out=std::move(result);return true;
}
bool padTerminalSegments(const std::vector<TerminalSegment> &spans,const TerminalTextPolicy &policy,TerminalTextAlignment alignment,ColoredTerminalLine *out,Tone padding){
    std::vector<TerminalTextSpan> bare;if(!out||!toneValid(padding)||!inputs(spans,bare))return false;
    TerminalTextRunLine layout;return padTerminalTextRuns(bare,policy,alignment,&layout)&&attachTones(std::move(layout),spans,padding,out);
}
bool renderWrappedTerminalSegments(const std::vector<TerminalSegment> &spans,const TerminalTextPolicy &policy,std::size_t maximumLines,ColorLevel color,std::vector<std::string> *out){
    if(!out||!colorValid(color))return false;
    std::vector<ColoredTerminalLine> rows;if(!wrapTerminalSegments(spans,policy,maximumLines,&rows))return false;
    std::vector<std::string> result;std::size_t bytes=0;
    for(const auto &row:rows){std::string text;if(!paintLayout(row,color,&text)||text.size()>262144-bytes)return false;bytes+=text.size();result.push_back(std::move(text));}
    *out=std::move(result);return true;
}
bool renderPaddedTerminalSegments(const std::vector<TerminalSegment> &spans,const TerminalTextPolicy &policy,TerminalTextAlignment alignment,ColorLevel color,std::string *out,Tone padding){
    if(!out||!colorValid(color))return false;
    ColoredTerminalLine layout;return padTerminalSegments(spans,policy,alignment,&layout,padding)&&paintLayout(layout,color,out);
}
} // namespace om
