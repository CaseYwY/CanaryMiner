#include "preferences_protocol.h"
#include "json_dom.h"
#include "localization.h"
#include "../core/http.h"
namespace om {
namespace {
bool kind(const JsonValue *value,PreferencesKind *out){
    if(!value||!value->isString())return false;
    for(unsigned i=0;i<8;++i)if(value->text==preferencesKindName(static_cast<PreferencesKind>(i))){*out=static_cast<PreferencesKind>(i);return true;}
    return false;
}
// Reuse the existing strict scalar/control policy without its 4096-byte message bound or splitting a UTF-8 scalar.
// Wire syntax is a single printable JSON line; original document newlines/controls must be JSON-escaped.
bool wireText(const std::string &text){
    if(text.empty()||text.size()>preferencesFrameLimit||text.back()!='\n')return false;
    for(size_t i=0;i+1<text.size();){
        const auto first=static_cast<unsigned char>(text[i]);
        if(first>=0x20&&first<=0x7e){++i;continue;}
        const size_t length=first>=0xc2&&first<=0xdf?2:first>=0xe0&&first<=0xef?3:first>=0xf0&&first<=0xf4?4:0;
        if(!length||length>text.size()-1-i||!i18n::safeMessageText(std::string_view(text).substr(i,length)))return false;
        i+=length;
    }
    return true;
}
bool read(const std::string &wire,const char *schema,size_t fields,JsonValue *root){
    if(!wireText(wire)||!parseJsonStrict(wire,root,nullptr)||!root->isObject()||root->members.size()!=fields)return false;
    const auto *version=root->find("schema");return version&&version->isString()&&version->text==schema;
}
bool reply(const PreferencesReply &value){
    if(!value.ticket||value.ticket==UINT64_MAX||!preferencesKindName(value.kind)||value.document.size()>preferencesDocumentLimit)return false;
    switch(value.status){
        case PreferencesReplyStatus::Ready:return value.kind==PreferencesKind::Ui&&!value.review&&value.document.empty();
        case PreferencesReplyStatus::Loaded:case PreferencesReplyStatus::Saved:return value.review==value.ticket&&!value.document.empty();
        case PreferencesReplyStatus::Rejected:case PreferencesReplyStatus::Unknown:return !value.review&&value.document.empty();
        default:return false;
    }
}
const char *statusName(PreferencesReplyStatus status){
    switch(status){case PreferencesReplyStatus::Ready:return "ready";case PreferencesReplyStatus::Loaded:return "loaded";case PreferencesReplyStatus::Saved:return "saved";case PreferencesReplyStatus::Rejected:return "rejected";case PreferencesReplyStatus::Unknown:return "unknown";default:return nullptr;}
}
}
const char *preferencesKindName(PreferencesKind value){
    static constexpr const char *names[]={"ui","runtime","manual","electricity","game","pool","language","tariff"};
    const auto index=static_cast<unsigned>(value);return index<8?names[index]:nullptr;
}
bool validPreferencesRequest(const PreferencesRequest &value){
    if(!value.ticket||value.ticket==UINT64_MAX||!preferencesKindName(value.kind)||value.document.size()>preferencesDocumentLimit)return false;
    bool targets=false;for(const auto &path:value.targets){if(!path.empty()){if(path.size()>4096||!i18n::safeMessageText(path))return false;targets=true;}}
    switch(value.operation){
        case PreferencesOperation::Init:return value.ticket==1&&value.kind==PreferencesKind::Ui&&!value.review&&targets&&value.document.empty();
        case PreferencesOperation::Load:return value.ticket>1&&!value.review&&!targets&&value.document.empty();
        case PreferencesOperation::Save:return value.ticket>1&&value.review&&value.review<value.ticket&&!targets&&!value.document.empty();
        default:return false;
    }
}
bool encodePreferencesRequest(const PreferencesRequest &value,std::string *out){
    if(!out||!validPreferencesRequest(value))return false;
    const char *operation=value.operation==PreferencesOperation::Init?"init":value.operation==PreferencesOperation::Load?"load":"save";
    std::string targets="[";for(size_t i=0;i<value.targets.size();++i){if(i)targets+=',';targets+=jsonQuoted(value.targets[i]);}targets+=']';
    const std::string wire="{\"schema\":\"CMPREFS1\",\"ticket\":"+std::to_string(value.ticket)+",\"operation\":"+jsonQuoted(operation)+",\"kind\":"+jsonQuoted(preferencesKindName(value.kind))+
        ",\"review\":"+std::to_string(value.review)+",\"targets\":"+targets+",\"document\":"+jsonQuoted(value.document)+"}\n";
    if(!wireText(wire))return false;
    *out=wire;return true;
}
bool decodePreferencesRequest(const std::string &wire,PreferencesRequest *out){
    JsonValue root;if(!out||!read(wire,"CMPREFS1",7,&root))return false;
    const auto *ticket=root.find("ticket"),*operation=root.find("operation"),*review=root.find("review"),*targets=root.find("targets"),*document=root.find("document");
    PreferencesRequest value;
    if(!ticket||!ticket->asUint64(&value.ticket)||!review||!review->asUint64(&value.review)||!kind(root.find("kind"),&value.kind)||!operation||!operation->isString()||!targets||!targets->isArray()||targets->items.size()!=8||!document||!document->isString())return false;
    if(operation->text=="init")value.operation=PreferencesOperation::Init;else if(operation->text=="load")value.operation=PreferencesOperation::Load;else if(operation->text=="save")value.operation=PreferencesOperation::Save;else return false;
    for(size_t i=0;i<8;++i){if(!targets->items[i].isString())return false;value.targets[i]=targets->items[i].text;}
    value.document=document->text;
    if(!validPreferencesRequest(value))return false;
    *out=std::move(value);return true;
}
bool encodePreferencesReply(const PreferencesReply &value,std::string *out){
    if(!out||!reply(value))return false;
    const std::string wire="{\"schema\":\"CMPREFR1\",\"ticket\":"+std::to_string(value.ticket)+",\"kind\":"+jsonQuoted(preferencesKindName(value.kind))+",\"review\":"+std::to_string(value.review)+
        ",\"status\":"+jsonQuoted(statusName(value.status))+",\"document\":"+jsonQuoted(value.document)+"}\n";
    if(!wireText(wire))return false;
    *out=wire;return true;
}
bool decodePreferencesReply(const std::string &wire,PreferencesReply *out){
    JsonValue root;if(!out||!read(wire,"CMPREFR1",6,&root))return false;
    const auto *ticket=root.find("ticket"),*review=root.find("review"),*status=root.find("status"),*document=root.find("document");PreferencesReply value;
    if(!ticket||!ticket->asUint64(&value.ticket)||!review||!review->asUint64(&value.review)||!kind(root.find("kind"),&value.kind)||!status||!status->isString()||!document||!document->isString())return false;
    bool found=false;for(unsigned i=0;i<5;++i)if(status->text==statusName(static_cast<PreferencesReplyStatus>(i))){value.status=static_cast<PreferencesReplyStatus>(i);found=true;break;}
    if(!found)return false;
    value.document=document->text;if(!reply(value))return false;
    *out=std::move(value);return true;
}
} // namespace om
