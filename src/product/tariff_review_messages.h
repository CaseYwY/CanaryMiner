// Private change-row envelopes only. Records retain original JSON/paths; formatting neither admits facts nor authorizes disclosure.
#pragma once
#include "localization.h"
namespace om { namespace i18n {
enum class TariffReviewMessageKey { Value, ValueRoot, Rows, RowsRoot, Count };
constexpr std::size_t tariffReviewMessageKeyCount=static_cast<std::size_t>(TariffReviewMessageKey::Count);
struct TariffReviewLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,tariffReviewMessageKeyCount> messages;};
inline const std::array<TariffReviewLocalePack,16> &tariffReviewLocalePacks(){
    static const std::array<TariffReviewLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{"{path}: {before} -> {after}",": {before} -> {after}","{path} rows: {before} -> {after}"," rows: {before} -> {after}"}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"{path}：{before} -> {after}",u8"：{before} -> {after}",u8"{path} 行数：{before} -> {after}",u8" 行数：{before} -> {after}"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"{path}：{before} -> {after}",u8"：{before} -> {after}",u8"{path} 列數：{before} -> {after}",u8" 列數：{before} -> {after}"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{"{path}: {before} -> {after}",": {before} -> {after}","{path} filas: {before} -> {after}"," filas: {before} -> {after}"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{"{path}: {before} -> {after}",": {before} -> {after}","{path} linhas: {before} -> {after}"," linhas: {before} -> {after}"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{"{path} : {before} -> {after}",": {before} -> {after}","{path} lignes : {before} -> {after}"," lignes : {before} -> {after}"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"{path}: {before} -> {after}",": {before} -> {after}","{path} Zeilen: {before} -> {after}"," Zeilen: {before} -> {after}"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{"{path}: {before} -> {after}",": {before} -> {after}",u8"{path} строки: {before} -> {after}",u8" строки: {before} -> {after}"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"{path}：{before} -> {after}",u8"：{before} -> {after}",u8"{path} 行数：{before} -> {after}",u8" 行数：{before} -> {after}"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{"{path}: {before} -> {after}",": {before} -> {after}",u8"{path} 행 수: {before} -> {after}",u8" 행 수: {before} -> {after}"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{"{path}: {before} -> {after}",": {before} -> {after}",u8"{path} صفوف: {before} -> {after}",u8" صفوف: {before} -> {after}"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{"{path}: {before} -> {after}",": {before} -> {after}",u8"{path} पंक्तियाँ: {before} -> {after}",u8" पंक्तियाँ: {before} -> {after}"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{"{path}: {before} -> {after}",": {before} -> {after}",u8"{path} সারি: {before} -> {after}",u8" সারি: {before} -> {after}"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"{path}: {before} -> {after}",": {before} -> {after}","{path} baris: {before} -> {after}"," baris: {before} -> {after}"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{"{path}: {before} -> {after}",": {before} -> {after}",u8"{path} satır: {before} -> {after}",u8" satır: {before} -> {after}"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{"{path}: {before} -> {after}",": {before} -> {after}",u8"{path} dòng: {before} -> {after}",u8" dòng: {before} -> {after}"}}}
    }};return packs;
}
inline std::string_view tariffReviewMessageKeyName(TariffReviewMessageKey key){
    static constexpr std::array<std::string_view,tariffReviewMessageKeyCount> names={{"tariff.review.value","tariff.review.value_root","tariff.review.rows","tariff.review.rows_root"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage tariffReviewMessage(std::string_view language,TariffReviewMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=tariffReviewMessageKeyCount)return {};
    const auto &source=tariffReviewLocalePacks()[0];const auto *selected=&source;for(const auto &pack:tariffReviewLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
// Source-only native interpolation is not a terminal-safe renderer or a JSON/path/number admission boundary.
inline std::string tariffReviewEnglish(TariffReviewMessageKey key,const MessageArguments &arguments={}){
    const auto text=tariffReviewMessage("en",key).text;std::vector<std::string_view> expected,supplied;
    if(text.empty()||arguments.size()>3||!templateParameters(text,&expected))return {};
    for(const auto &arg:arguments)supplied.push_back(arg.first);
    std::sort(supplied.begin(),supplied.end());if(supplied!=expected)return {};
    std::string result;std::size_t at=0;
    while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){result.append(text.substr(at));break;}result.append(text.substr(at,start-at));const auto end=text.find('}',start);
        const auto name=text.substr(start+1,end-start-1);const auto found=std::find_if(arguments.begin(),arguments.end(),[&](const auto &arg){return arg.first==name;});if(found==arguments.end())return {};
        result+=found->second;at=end+1;
    }
    return result;
}
} } // namespace om::i18n
