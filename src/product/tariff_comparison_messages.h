// Source messages for the whole-meter comparison. Locale requests fall back to English.
#pragma once
#include "localization.h"

namespace om { namespace i18n {
enum class TariffComparisonMessageKey {
    Hint, ShortTitle, Resize, Back, Title, NeedDeclaration, Span, Instruction, WithoutWatts, WithWatts,
    Footer, NeedWatts, Clock, Pending, Failed, ModelStatus, DisplayRange, WithoutCharge, WithCharge, Difference,
    RetryAfterCancel, Cycles, EndUnavailable, CycleStatus, ChargedDay,
    WithoutApparent, WithApparent, ApparentInput, ApparentFactorInput, NeedApparent, NeedApparentFactor,
    WithoutNetWatts, WithNetWatts, NetInstruction, NetCycles, NeedNetWatts, WithoutNetCharge, WithNetCharge,
    WithoutCreditCharge, WithCreditCharge
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
        "Previous comparison cancelled. Press Enter again to retry.",
        "[Left/Right] Cycles {count}/{max} | Constant watts",
        "UTC end unavailable for this span.",
        "Comparison unavailable in cycle {cycle}: {status}",
        "Cycle {cycle} starts within a charged tariff day.",
        "Without mining: {power}", "With mining:    {power}",
        "Enter whole-meter watts/VA for each scenario.",
        "Enter whole-meter watts/VA or watts@power_factor.",
        "Enter watts/VA for both scenarios. VA must be at least the watts.",
        "Enter watts/VA or watts@power_factor for both scenarios. The factor must be greater than 0 and up to 1.",
        "Without mining (net W): {watts}", "With mining (net W):    {watts}",
        "Net watts per scenario (+import, -export).",
        "[Left/Right] {count}/{max} | +import -export",
        "Enter signed net watts for each scenario (+import, -export).",
        "Net without mining: {amount} {currency}", "Net with mining:    {amount} {currency}",
        "Cash due without mining: {amount} {currency}", "Cash due with mining:    {amount} {currency}"
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
