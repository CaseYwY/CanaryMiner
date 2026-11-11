// Private one-shot product-config read, separate from the eight preference slots. Never public output.
#pragma once
#include "json_dom.h"
#include "localization.h"
#include <cstdint>
#include <string>

namespace om {
constexpr size_t startupConfigDocumentLimit=1u<<20; // the existing strict config parser's maximum, not a new smaller limit
constexpr size_t startupConfigRequestLimit=32u<<10,startupConfigHeaderLimit=256;
static_assert(startupConfigDocumentLimit==json_dom_detail::kMaxInput,"Review config wire bounds when parser bounds change");
struct StartupConfigRequest {uint64_t ticket=0;std::string target;};
enum class StartupConfigReadStatus { Loaded, Unavailable, Oversized };
struct StartupConfigHeader {uint64_t ticket=0;StartupConfigReadStatus status=StartupConfigReadStatus::Unavailable;size_t bytes=0;};
inline bool encodeStartupConfigRequest(const StartupConfigRequest &value,std::string *out){
    if(!out||!value.ticket||value.ticket==UINT64_MAX||value.target.empty()||value.target.size()>4096||!i18n::safeMessageText(value.target))return false;
    const auto wire="{\"schema\":\"CMCFGQ1\",\"ticket\":"+std::to_string(value.ticket)+",\"target\":"+jsonQuote(value.target)+"}\n";
    if(wire.size()>startupConfigRequestLimit)return false;
    *out=wire;return true;
}
inline bool decodeStartupConfigRequest(const std::string &wire,StartupConfigRequest *out){
    if(!out||wire.empty()||wire.size()>startupConfigRequestLimit||wire.back()!='\n')return false;
    JsonValue root;if(!parseJsonStrict(wire,&root,nullptr)||!root.isObject()||root.members.size()!=3)return false;
    const auto *schema=root.find("schema"),*ticket=root.find("ticket"),*target=root.find("target");StartupConfigRequest value;
    if(!schema||!schema->isString()||schema->text!="CMCFGQ1"||!ticket||!ticket->asUint64(&value.ticket)||!target||!target->isString())return false;
    value.target=target->text;std::string canonical;
    if(!encodeStartupConfigRequest(value,&canonical)||canonical!=wire)return false;
    *out=std::move(value);return true;
}
inline bool encodeStartupConfigHeader(const StartupConfigHeader &value,std::string *out){
    if(!out||!value.ticket||value.ticket==UINT64_MAX||value.bytes>startupConfigDocumentLimit)return false;
    const char *status=nullptr;
    switch(value.status){
        case StartupConfigReadStatus::Loaded:status="loaded";break;
        case StartupConfigReadStatus::Unavailable:status="unavailable";break;
        case StartupConfigReadStatus::Oversized:status="oversized";break;
        default:return false;
    }
    if(value.status!=StartupConfigReadStatus::Loaded&&value.bytes)return false;
    const auto wire="{\"schema\":\"CMCFGR1\",\"ticket\":"+std::to_string(value.ticket)+",\"status\":"+jsonQuote(status)+",\"bytes\":"+std::to_string(value.bytes)+"}\n";
    if(wire.size()>startupConfigHeaderLimit)return false;
    *out=wire;return true;
}
inline bool decodeStartupConfigHeader(const std::string &wire,StartupConfigHeader *out){
    if(!out||wire.empty()||wire.size()>startupConfigHeaderLimit||wire.back()!='\n')return false;
    JsonValue root;if(!parseJsonStrict(wire,&root,nullptr)||!root.isObject()||root.members.size()!=4)return false;
    const auto *schema=root.find("schema"),*ticket=root.find("ticket"),*status=root.find("status"),*bytes=root.find("bytes");
    StartupConfigHeader value;uint64_t count=0;
    if(!schema||!schema->isString()||schema->text!="CMCFGR1"||!ticket||!ticket->asUint64(&value.ticket)||!status||!status->isString()||!bytes||!bytes->asUint64(&count)||count>startupConfigDocumentLimit)return false;
    if(status->text=="loaded")value.status=StartupConfigReadStatus::Loaded;
    else if(status->text=="unavailable")value.status=StartupConfigReadStatus::Unavailable;
    else if(status->text=="oversized")value.status=StartupConfigReadStatus::Oversized;
    else return false;
    value.bytes=static_cast<size_t>(count);std::string canonical;
    if(!encodeStartupConfigHeader(value,&canonical)||canonical!=wire)return false;
    *out=value;return true;
}
} // namespace om
