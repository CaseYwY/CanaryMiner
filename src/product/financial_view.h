// Read-only presentation contracts. No ledger storage, API requests, trade or accounting inference.
#pragma once

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace om {

struct TokenProductionView {
    std::optional<double> tokens;  // display projection of exact backend quantities, not the ledger representation
    bool complete = false;
};

struct MarketPriceView {
    bool fresh = false;
    std::optional<double> latestTokenUsdt, latestUsdtUsd;
    std::optional<double> averageTokenUsd1h;  // must already have time-aligned conversion/complete hour coverage
    std::string source;
    uint64_t ageSeconds = 0;
};

enum class FinancialEnergyBasis { GpuBoardProxy, WholeSystem, IncrementalWall };

struct FinancialView {
    bool enabled = false;  // only set by an integrated provider; no fake enabled switch for unfinished backends
    MarketPriceView market;
    TokenProductionView todayForecast, actual24h, actualCumulative;
    bool electricityConfigured = false, electricityHorizonMatched = false;
    std::optional<double> electricityTodayUsd;
    FinancialEnergyBasis energyBasis = FinancialEnergyBasis::GpuBoardProxy;
    std::vector<std::string> exactLines; // prevalidated exact backend projection; empty keeps the legacy synthetic display contract
};

inline std::string financialDisplayNumber(double number, unsigned decimals) {
    if (!std::isfinite(number)) return "N/A";
    std::ostringstream out;
    out.imbue(std::locale::classic());
    if (std::abs(number) >= 1e12) out << std::scientific;
    else out << std::fixed;
    out << std::setprecision(static_cast<int>(decimals)) << number;
    return out.str();
}

inline std::optional<double> latestTokenPriceUsd(const MarketPriceView &market) {
    if (!market.fresh || !market.latestTokenUsdt || !market.latestUsdtUsd || !std::isfinite(*market.latestTokenUsdt) ||
        !std::isfinite(*market.latestUsdtUsd) || *market.latestTokenUsdt <= 0 || *market.latestUsdtUsd <= 0) return std::nullopt;
    const double result = *market.latestTokenUsdt * *market.latestUsdtUsd;
    return std::isfinite(result) && result > 0 ? std::optional<double>(result) : std::nullopt;
}

inline std::optional<double> productionUsdNow(const TokenProductionView &production, const MarketPriceView &market) {
    const auto price = latestTokenPriceUsd(market);
    if (!production.tokens || !std::isfinite(*production.tokens) || !price) return std::nullopt;
    const double value = *production.tokens * *price;
    return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}

inline std::vector<std::string> financialViewLines(const FinancialView &view) {
    if (!view.enabled) return {};
    if (!view.exactLines.empty()) return view.exactLines;
    std::vector<std::string> lines;
    const auto &market = view.market;
    const auto latest = latestTokenPriceUsd(market);
    const auto average = market.averageTokenUsd1h;
    const std::string avgText = market.fresh && average && std::isfinite(*average) && *average > 0 ?
        "$" + financialDisplayNumber(*average, 6) : "N/A";
    lines.push_back("Price USD: 1h average " + avgText + " | latest " + (latest ? "$" + financialDisplayNumber(*latest, 6) : "N/A"));
    lines.push_back("Quote: " + (market.source.empty() ? std::string("not configured") : market.source) + " | age " +
                    std::to_string(market.ageSeconds) + "s | " + (market.fresh ? "fresh" : "unavailable/stale"));
    if (market.fresh && market.latestTokenUsdt && std::isfinite(*market.latestTokenUsdt) && *market.latestTokenUsdt > 0)
        lines.push_back("Latest token/USDT: " + financialDisplayNumber(*market.latestTokenUsdt, 6) + " (USDT is not USD)");
    auto production = [&](const char *label, const TokenProductionView &value) {
        const bool hasTokens = value.tokens && std::isfinite(*value.tokens);
        const auto usd = productionUsdNow(value, market);
        const std::string scope = hasTokens && !value.complete ? " [partial coverage]" : "";
        lines.push_back(std::string(label) + ": " + (hasTokens ? financialDisplayNumber(*value.tokens, 6) + " PRL" : "N/A") +
                        " | at latest price " + (usd ? "$" + financialDisplayNumber(*usd, 2) : "N/A") + scope);
    };
    production("Today forecast", view.todayForecast);
    production("Actual credited 24h", view.actual24h);
    production("Actual credited cumulative", view.actualCumulative);
    if (view.electricityConfigured) {
        const auto revenue = productionUsdNow(view.todayForecast, market);
        const bool costValid = view.electricityTodayUsd && std::isfinite(*view.electricityTodayUsd);
        const double net = revenue && costValid ? *revenue - *view.electricityTodayUsd : 0;
        const bool basisKnown = view.energyBasis == FinancialEnergyBasis::GpuBoardProxy || view.energyBasis == FinancialEnergyBasis::WholeSystem ||
                                view.energyBasis == FinancialEnergyBasis::IncrementalWall;
        const bool available = basisKnown && view.todayForecast.complete && view.electricityHorizonMatched && revenue && costValid && std::isfinite(net);
        const std::string basis = view.energyBasis == FinancialEnergyBasis::GpuBoardProxy ? "GPU-board proxy; not whole-PC" :
            view.energyBasis == FinancialEnergyBasis::WholeSystem ? "whole-system estimate; not isolated mining increment" :
            view.energyBasis == FinancialEnergyBasis::IncrementalWall ? "matched incremental-wall estimate" : "unknown energy basis";
        lines.push_back("Estimated net today USD: " + (available ? "$" + financialDisplayNumber(net, 2) : "N/A (missing/mismatched inputs)") +
                        " | " + basis);
    }
    lines.push_back("Fiat values are production references; sales and current holdings are unknown.");
    return lines;
}

}  // namespace om
