// Private local helper protocol only. Documents/roots are never public stats, logs, diagnostics or upload payloads.
#pragma once
#include <cstddef>
#include <cstdint>
#include <array>
#include <string>
namespace om {
constexpr size_t preferencesDocumentLimit=65536,preferencesFrameLimit=192*1024;
enum class PreferencesKind { Ui, Runtime, Manual, Electricity, Game, Pool, Language, Tariff };
enum class PreferencesOperation { Init, Load, Save };
enum class PreferencesReplyStatus { Ready, Loaded, Saved, Rejected, Unknown };
struct PreferencesRequest {
    uint64_t ticket=0,review=0;
    PreferencesKind kind=PreferencesKind::Ui;
    PreferencesOperation operation=PreferencesOperation::Load;
    std::array<std::string,8> targets{}; // frozen explicit configured paths at Init only; empty slot is unavailable
    std::string document;
};
struct PreferencesReply {
    uint64_t ticket=0,review=0;
    PreferencesKind kind=PreferencesKind::Ui;
    PreferencesReplyStatus status=PreferencesReplyStatus::Rejected;
    std::string document;
};
const char *preferencesKindName(PreferencesKind);
bool validPreferencesRequest(const PreferencesRequest &);
bool encodePreferencesRequest(const PreferencesRequest &,std::string *);
bool decodePreferencesRequest(const std::string &,PreferencesRequest *);
bool encodePreferencesReply(const PreferencesReply &,std::string *);
bool decodePreferencesReply(const std::string &,PreferencesReply *);
} // namespace om
