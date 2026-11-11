// Private wire grammar only, never source/consent/OS authority. No file, process, device or network operations.
#include "product/preferences_protocol.h"
#include "product_test_util.h"
#include <vector>
int main(){using namespace om;PreferencesRequest request;request.ticket=1;request.operation=PreferencesOperation::Init;request.targets[0]="/fixture-state/ui.json";std::string wire;
    CHECK(encodePreferencesRequest(request,&wire));PreferencesRequest parsed;CHECK(decodePreferencesRequest(wire,&parsed)&&parsed.targets==request.targets);
    CHECK(!encodePreferencesRequest(request,nullptr)&&!decodePreferencesRequest(wire,nullptr));
    for(unsigned i=0;i<8;++i){
        request={};request.ticket=2;request.kind=static_cast<PreferencesKind>(i);CHECK(preferencesKindName(request.kind));
        CHECK(encodePreferencesRequest(request,&wire)&&decodePreferencesRequest(wire,&parsed)&&parsed.kind==request.kind&&parsed.ticket==2&&parsed.document.empty());
        request.operation=PreferencesOperation::Save;request.ticket=3;request.review=2;request.document="{\"fixture-private-original\":\"quoted\\\"value\"}\n";
        CHECK(encodePreferencesRequest(request,&wire)&&decodePreferencesRequest(wire,&parsed)&&parsed.document==request.document&&parsed.review==2);
        for(auto state:{PreferencesReplyStatus::Loaded,PreferencesReplyStatus::Saved,PreferencesReplyStatus::Rejected,PreferencesReplyStatus::Unknown}){
            PreferencesReply value;value.kind=request.kind;value.ticket=3;value.status=state;
            if(state==PreferencesReplyStatus::Loaded||state==PreferencesReplyStatus::Saved){value.review=3;value.document=request.document;}
            PreferencesReply decoded;CHECK(encodePreferencesReply(value,&wire)&&decodePreferencesReply(wire,&decoded)&&decoded.kind==value.kind&&decoded.status==state&&decoded.document==value.document);
        }
    }
    const auto reject=[&](PreferencesRequest invalid){std::string output="previous";CHECK(!encodePreferencesRequest(invalid,&output)&&output=="previous");};
    for(uint64_t ticket:{uint64_t{0},UINT64_MAX}){auto invalid=request;invalid.ticket=ticket;reject(invalid);}
    for(unsigned invalid:{8u,255u}){auto bad=request;bad.kind=static_cast<PreferencesKind>(invalid);reject(bad);CHECK(!preferencesKindName(bad.kind));}
    {auto invalid=request;invalid.operation=static_cast<PreferencesOperation>(9);reject(invalid);}
    {auto invalid=request;invalid.review=0;reject(invalid);invalid.review=3;reject(invalid);invalid.review=4;reject(invalid);}
    {auto invalid=request;invalid.targets[0]="/fixture-private-path";reject(invalid);invalid.targets[0].clear();invalid.document.clear();reject(invalid);invalid.document.assign(preferencesDocumentLimit+1,'a');reject(invalid);}
    request.kind=PreferencesKind::Ui;request.operation=PreferencesOperation::Init;request.ticket=1;request.review=0;request.document.clear();request.targets[0]="/fixture-state/ui.json";
    for(const auto &path:std::vector<std::string>{"",std::string(4097,'a'),"/fixture\npath",std::string("/fixture\0path",13),std::string("/fixture-\xc0\x80",11)}){auto invalid=request;invalid.targets[0]=path;reject(invalid);}
    for(unsigned invalid:{1u,7u}){auto bad=request;bad.kind=static_cast<PreferencesKind>(invalid);reject(bad);}
    {auto invalid=request;invalid.ticket=2;reject(invalid);invalid=request;invalid.review=1;reject(invalid);invalid=request;invalid.document="{}";reject(invalid);}
    CHECK(encodePreferencesRequest(request,&wire));const auto original=wire;parsed.targets[0]="previous-original";parsed.ticket=42;
    for(const auto &bad:std::vector<std::string>{"", "[]\n",original+original,original+"\n",original.substr(0,original.size()-1),"{}\n",std::string(preferencesFrameLimit+1,'x'),"{\"schema\":\"CMPREFS1\",\"ticket\":1,\"ticket\":2}\n"})
        CHECK(!decodePreferencesRequest(bad,&parsed)&&parsed.targets[0]=="previous-original"&&parsed.ticket==42);
    for(const auto &replacement:{"CMPREFS9","CMPREFS1-private"}){auto bad=original;bad.replace(bad.find("CMPREFS1"),8,replacement);CHECK(!decodePreferencesRequest(bad,&parsed));}
    for(const auto &key:{"ticket","operation","kind","review","targets","document"}){auto bad=original;bad.replace(bad.find(std::string("\"")+key+"\""),std::string(key).size()+2,"\"unknown\"");CHECK(!decodePreferencesRequest(bad,&parsed));}
    request={};request.operation=PreferencesOperation::Save;request.ticket=3;request.review=2;request.document.assign(preferencesDocumentLimit,'"');
    CHECK(encodePreferencesRequest(request,&wire)&&wire.size()<preferencesFrameLimit&&decodePreferencesRequest(wire,&parsed)&&parsed.document==request.document);
    request.document="{\"fixture\":\"\xc0\x80\"}";reject(request);
    PreferencesReply value;value.ticket=1;value.status=PreferencesReplyStatus::Ready;CHECK(encodePreferencesReply(value,&wire));
    PreferencesReply decoded;CHECK(decodePreferencesReply(wire,&decoded)&&decoded.status==PreferencesReplyStatus::Ready);
    const auto rejectReply=[&](PreferencesReply invalid){std::string output="previous";CHECK(!encodePreferencesReply(invalid,&output)&&output=="previous");};
    {auto invalid=value;invalid.review=1;rejectReply(invalid);invalid=value;invalid.document="{}";rejectReply(invalid);invalid=value;invalid.kind=PreferencesKind::Runtime;rejectReply(invalid);invalid=value;invalid.status=static_cast<PreferencesReplyStatus>(9);rejectReply(invalid);}
    value.ticket=3;value.status=PreferencesReplyStatus::Saved;value.review=3;value.document="{}\n";CHECK(encodePreferencesReply(value,&wire));
    for(auto state:{PreferencesReplyStatus::Loaded,PreferencesReplyStatus::Saved}){auto invalid=value;invalid.status=state;invalid.review=2;rejectReply(invalid);invalid.review=3;invalid.document.clear();rejectReply(invalid);}
    decoded.document="previous-original";
    for(const auto &bad:std::vector<std::string>{"", "[]\n",wire+wire,wire.substr(0,wire.size()-1),"{}\n"})CHECK(!decodePreferencesReply(bad,&decoded)&&decoded.document=="previous-original");
    return finishTests("private bounded preference helper wire grammar and transactional refusal");
}
