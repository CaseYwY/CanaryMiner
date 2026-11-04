#include "terminal_number.h"
#include <algorithm>
namespace om {
bool formatTerminalQuantity(const TerminalQuantityRequest &request,TerminalQuantity *out){
    if(!out||request.label.size()>32||static_cast<unsigned>(request.tone)>static_cast<unsigned>(Tone::Green))return false;
    if(!std::all_of(request.label.begin(),request.label.end(),[](unsigned char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='/'||c=='%'||c=='+'||c=='-';}))return false;
    TerminalQuantity result;if(!formatPresentationNumber(request.number,&result.number))return false;
    const auto label=request.label.empty()?std::string{}:" "+request.label;
    result.segment=TerminalSegment{TerminalTextSpan{result.number.text+label,result.number.ascii+label,false,TerminalTextAtom::LeftToRight},request.tone};
    *out=std::move(result);return true;
}
} // namespace om
