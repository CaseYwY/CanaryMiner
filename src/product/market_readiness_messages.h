// English source for public data readiness and the economic-mode request.
#pragma once
#include "localization.h"

namespace om { namespace i18n {
enum class MarketReadinessMessageKey {
    Entry, Title, Resize, Back, Gate, Kraken, Latest, GateHour, KrakenHour, Approval, Reward, Footer,
    Economy, EconomyChange, EconomyChangeCompact, EconomyOff, EconomyOffCompact, EconomySaved
};

inline LocalizedMessage marketReadinessMessage(std::string_view locale, MarketReadinessMessageKey key) {
    static constexpr std::string_view messages[] = {
        "Public feed readiness [Enter]", "Public feed readiness", "Resize to 60 x 12", "[Esc] Back [Q] Stop",
        "Gate Spot REST | PEARL_USDT", "Kraken Spot REST | USDT/USD",
        "Latest: documented last-trade price. Trade timestamp unavailable from the ticker.",
        "1 hour: candle response shape and timestamp anchor unresolved.",
        "1 hour: candle timestamp anchor unresolved. The final REST candle is provisional.",
        "Public polling requires a capable build and explicit launch permission. Release qualification is pending.",
        "Reward data: no qualified source.",
        "[Tab/Arrows] Source [Esc] Back [Q/Ctrl+C] Stop",
        "Economy req [{request}]", "Economic mode request: {before} -> {after}", "Econ req {before} -> {after}",
        "Economic mode is inactive. Requests require qualified price and cost inputs.",
        "Economy inactive", "Request saved. Economic mode is inactive."
    };
    const auto index = static_cast<size_t>(key);
    if (index >= sizeof(messages) / sizeof(*messages)) return {};
    return {messages[index], "en", ReviewStatus::Source, locale != "en", false};
}

inline std::string marketReadinessEnglish(MarketReadinessMessageKey key, const MessageArguments &args = {}) {
    std::string result;
    formatMessageTemplate(marketReadinessMessage("en", key).text, args, &result);
    return result;
}
} }  // namespace om::i18n
