// Source messages for the whole-meter comparison. Locale requests fall back to English.
#pragma once
#include "localization.h"

namespace om { namespace i18n {
enum class TariffComparisonMessageKey {
    Hint, ShortTitle, Resize, Back, Title, NeedDeclaration, Span, Instruction, WithoutWatts, WithWatts,
    Footer, NeedWatts, Clock, Pending, Failed, ModelStatus, DisplayRange, WithoutCharge, WithCharge, Difference,
    RetryAfterCancel
};

inline LocalizedMessage tariffComparisonMessage(std::string_view locale, TariffComparisonMessageKey key) {
    static constexpr std::string_view messages[] = {
        "[P] Compare whole-meter", "Whole-meter comparison", "Resize to 60 x 12", "[Esc] Back [Q] Stop",
        "Whole-meter comparison | HYPOTHETICAL", "Save a valid combined tariff and opening first.",
        "Saved opening | UTC ms {begin} to {end}", "Enter constant whole-meter import for that entire span.",
        "Without mining (W): {watts}", "With mining (W):    {watts}", "[Tab] Field [Enter] Compare [Esc] Back [Q] Stop",
        "Enter both watt values as nonnegative decimals.", "Comparison unavailable at the current local time.",
        "Calculating the saved scenario...", "Comparison unavailable.", "Comparison unavailable: {status}",
        "Comparison unavailable: display range exceeded.",
        "Remaining without mining: {amount} {currency}", "Remaining with mining:    {amount} {currency}",
        "Difference (with - without): {amount} {currency}",
        "Previous comparison cancelled. Press Enter again to retry."
    };
    const auto index = static_cast<size_t>(key);
    if (index >= sizeof(messages) / sizeof(*messages)) return {};
    return {messages[index], "en", ReviewStatus::Source, locale != "en", false};
}

inline std::string tariffComparisonEnglish(TariffComparisonMessageKey key, const MessageArguments &args = {}) {
    std::string result;
    formatMessageTemplate(tariffComparisonMessage("en", key).text, args, &result);
    return result;
}
} }  // namespace om::i18n
