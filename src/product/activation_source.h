// ASCII-only source boundary for low-level helpers. No locale catalog, renderer or permission authority.
#pragma once
#include <array>
#include <cstddef>
#include <string_view>
namespace om { namespace i18n {
enum class ActivationMessageKey { Presentation, WorkBoundary, NextStart, SeparatePermission, Unsupported, Count };
constexpr std::size_t activationMessageKeyCount=static_cast<std::size_t>(ActivationMessageKey::Count);
inline std::string_view activationEnglishText(ActivationMessageKey key){
    static constexpr std::array<std::string_view,activationMessageKeyCount> text={{"live presentation","safe work boundary","next explicit start; no automatic restart","separate permission required","unsupported"}};
    const auto index=static_cast<std::size_t>(key);return index<text.size()?text[index]:std::string_view{};
}
} } // namespace om::i18n
