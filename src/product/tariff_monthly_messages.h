// Source messages for the monthly billing-cycle editor.
#pragma once
#include "localization.h"

namespace om { namespace i18n {
enum class TariffMonthlyMessageKey { Toggle, Derive, Enabled, Day, RetainAmounts, Derived, Review, Count };

inline LocalizedMessage tariffMonthlyMessage(std::string_view locale,TariffMonthlyMessageKey key) {
    static constexpr std::array<std::string_view,static_cast<size_t>(TariffMonthlyMessageKey::Count)> messages = {{
        "[M] Cycle policy", "[G] Derive cycle",
        "Enter the reset day and check the reset clock. Use G to derive dates.",
        "Day 1-31 at 00:00. Short months use their last day.",
        "Clear both opening amounts before changing the cycle start.",
        "Draft cycle dates derived. Review before saving.",
        "Monthly reset at 00:00 in the declared reset clock. Draft dates only."
    }};
    const auto index=static_cast<size_t>(key);
    if(index>=messages.size())return {};
    return {messages[index],"en",ReviewStatus::Source,locale!="en",false};
}

inline std::string tariffMonthlyEnglish(TariffMonthlyMessageKey key) {
    return std::string(tariffMonthlyMessage("en",key).text);
}
} } // namespace om::i18n
