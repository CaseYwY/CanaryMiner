// Explicit production wire domain. No change to bounded v1/v2 framing.
#pragma once
#include "pearl_guard_retirement_channel.h"
namespace om { namespace retirement_production {
using namespace retirement_experiment;
constexpr uint32_t counterLimit=INT32_MAX;
inline bool encodeV3(const Frame &frame,Bytes *out) {
    if (!out || frame.kind<1 || frame.kind>3 || !frame.reservation || frame.reservation>counterLimit) return false;
    Frame compatible=frame;compatible.kind=frame.kind==3?2:frame.kind;compatible.reservation=1;
    if (!encode(compatible,out))return false;
    (*out)[7]='3';(*out)[8]=frame.kind;put(out->data()+48,frame.reservation,4);return true;
}
inline bool decodeV3(const void *raw,size_t size,Frame *out) {
    if (!raw || !out || size!=frameBytes)return false;
    Bytes compatible;std::memcpy(compatible.data(),raw,size);
    if (std::memcmp(compatible.data(),"PRLGRET3",8) || compatible[8]<1 || compatible[8]>3)return false;
    const auto kind=compatible[8];const auto reservation=get(compatible.data()+48,4);
    if (!reservation || reservation>counterLimit)return false;
    compatible[7]='1';compatible[8]=kind==3?2:kind;put(compatible.data()+48,1,4);
    Frame value;
    if (!decode(compatible.data(),compatible.size(),&value))return false;
    value.kind=kind;value.reservation=uint32_t(reservation);*out=value;return true;
}
}} // namespace om::retirement_production
