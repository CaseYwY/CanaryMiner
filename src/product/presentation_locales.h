// Fixed presentation metadata only. No ICU call, locale discovery, geography, money, timezone or file access.
#pragma once
#include <array>
#include <string_view>
namespace om {
namespace presentation_detail {
struct FormattingLocale {const char *id,*icu;};
inline const std::array<FormattingLocale,31> &formattingLocales(){
    static constexpr std::array<FormattingLocale,31> values={{{"en","en"},{"zh-Hans","zh_Hans"},{"zh-Hant","zh_Hant"},{"es","es"},{"pt-BR","pt_BR"},{"fr","fr"},{"de","de"},{"ru","ru"},
        {"ja","ja"},{"ko","ko"},{"ar","ar"},{"hi","hi"},{"bn","bn"},{"id","id"},{"tr","tr"},{"vi","vi"},{"en-US","en_US"},{"en-GB","en_GB"},{"fr-FR","fr_FR"},
        {"de-DE","de_DE"},{"es-ES","es_ES"},{"ar-EG","ar_EG"},{"hi-IN","hi_IN"},{"bn-BD","bn_BD"},{"zh-CN","zh_CN"},{"zh-TW","zh_TW"},{"ja-JP","ja_JP"},
        {"ko-KR","ko_KR"},{"id-ID","id_ID"},{"tr-TR","tr_TR"},{"vi-VN","vi_VN"}}};return values;
}
}
inline bool knownPresentationFormattingLocale(std::string_view id){for(const auto &entry:presentation_detail::formattingLocales())if(id==entry.id)return true;return false;}
} // namespace om
