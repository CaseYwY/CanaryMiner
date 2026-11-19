#pragma once
#include "localization.h"

namespace om { namespace i18n {
enum class MarketTickerSettingsKey {
    Entry, Title, Request, Change, Scope, Conflict, Unavailable, Current, CliBlocked, SaveNotice, NextStart, Footer,
    ReviewFooter, CompactTitle, CompactRequest, CompactReview, CompactChange, CompactScope, CompactConflict,
    CompactUnavailable, CompactCurrent, CompactCliBlocked, CompactNextStart, CompactGate, CompactKraken, CompactFooter, CompactReviewFooter, Count
};

inline LocalizedMessage marketTickerSettingsMessage(std::string_view locale, MarketTickerSettingsKey key) {
    static constexpr std::array<std::string_view,static_cast<size_t>(MarketTickerSettingsKey::Count)> messages = {{
        "Public ticker request [Enter]", "CanaryMiner / Public ticker request", "Public tickers [{request}]",
        "Public tickers: {before} -> {after}", "Latest only: Gate PRL/USDT and Kraken USDT/USD.",
        "If manual references are enabled too, both sources are withheld.",
        "Polling unavailable. Source-use and deployment approval pending.",
        "Current session request: {request}", "This launch: --no-market withholds both sources.",
        "Request saved.", "Applies to the next explicit mining session.",
        "[Space] Change [V/F2] Review [Esc] Back [Q] Stop",
        "[Arrows] Scroll [Enter] Save [Esc] Edit [Q] Stop",
        "Public tickers", "Tickers [{request}]", "Review tickers",
        "Ticker: {before} -> {after}", "Latest refs only", "Both on: withheld", "Polling unavailable",
        "Session req: {request}", "CLI: --no-market", "Next explicit run", "Gate PRL/USDT", "Kraken USDT/USD",
        "Space V Esc Q", "Arrows Enter Esc Q"
    }};
    const auto index=static_cast<size_t>(key);
    if(index>=messages.size())return {};
    return {messages[index],"en",ReviewStatus::Source,locale!="en",false};
}

inline std::string marketTickerSettingsEnglish(MarketTickerSettingsKey key, const MessageArguments &args = {}) {
    std::string result;
    formatMessageTemplate(marketTickerSettingsMessage("en",key).text,args,&result);
    return result;
}
} } // namespace om::i18n
