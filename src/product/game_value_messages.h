// Game preference-review envelopes only. Invariant requests and human adapter descriptions remain distinct.
#pragma once
#include "localization.h"
namespace om { namespace i18n {
enum class GameValueMessageKey { FieldValue, FieldChange, ScopeValue, ScopeChange, EditPrompt, EditInput, EffectiveFrozen, Count };
constexpr std::size_t gameValueMessageKeyCount=static_cast<std::size_t>(GameValueMessageKey::Count);
struct GameValueLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,gameValueMessageKeyCount> messages;};
inline const std::array<GameValueLocalePack,16> &gameValueLocalePacks(){
    static const std::array<GameValueLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}:","> {input}_","Effective: {mode} (frozen)"}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：","> {input}_",u8"实际：{mode}（冻结）"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：","> {input}_",u8"實際：{mode}（凍結）"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}:","> {input}_","Efectivo: {mode} (congelado)"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}:","> {input}_","Efetivo: {mode} (congelado)"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{"{field} : {value}","{field} : {before} -> {after}","{field} : {value}","{field} : {before} -> {after}","{field} :","> {input}_",u8"Effectif : {mode} (figé)"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}:","> {input}_","Wirksam: {mode} (eingefroren)"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}:","> {input}_",u8"Действующее: {mode} (заморожено)"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：","> {input}_",u8"実際：{mode}（固定）"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}:","> {input}_",u8"실제: {mode} (고정)"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}:","> {input}_",u8"الفعال: {mode} (مجمد)"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}:","> {input}_",u8"प्रभावी: {mode} (स्थिर)"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}:","> {input}_",u8"কার্যকর: {mode} (স্থির)"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}:","> {input}_","Efektif: {mode} (beku)"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}:","> {input}_",u8"Etkin: {mode} (donmuş)"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}:","> {input}_",u8"Thực tế: {mode} (cố định)"}}}
    }};return packs;
}
inline std::string_view gameValueMessageKeyName(GameValueMessageKey key){
    static constexpr std::array<std::string_view,gameValueMessageKeyCount> names={{"game.envelope.field_value","game.envelope.field_change","game.envelope.scope_value","game.envelope.scope_change","game.envelope.edit_prompt","game.envelope.edit_input","game.envelope.effective_frozen"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage gameValueMessage(std::string_view language,GameValueMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=gameValueMessageKeyCount)return {};
    const auto &source=gameValueLocalePacks()[0];const auto *selected=&source;for(const auto &pack:gameValueLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
inline std::string gameValueEnglish(GameValueMessageKey key,const MessageArguments &arguments){
    if(arguments.size()>3)return {};
    const auto text=gameValueMessage("en",key).text;std::vector<std::string_view> expected,supplied;
    if(text.empty()||!templateParameters(text,&expected))return {};
    for(const auto &arg:arguments){if(arg.first.size()>32||arg.second.size()>128||!std::all_of(arg.second.begin(),arg.second.end(),[](unsigned char c){return c>=32&&c<=126;}))return {};supplied.push_back(arg.first);}
    std::sort(supplied.begin(),supplied.end());if(supplied!=expected)return {};
    std::string result;std::size_t at=0;
    while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){result.append(text.substr(at));break;}result.append(text.substr(at,start-at));const auto end=text.find('}',start);
        const auto name=text.substr(start+1,end-start-1);const auto found=std::find_if(arguments.begin(),arguments.end(),[&](const auto &arg){return arg.first==name;});if(found==arguments.end())return {};
        result+=found->second;if(result.size()>4096)return {};at=end+1;
    }
    return result.size()<=4096?result:std::string{};
}
} } // namespace om::i18n
