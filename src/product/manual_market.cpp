#include "manual_market.h"
#include "exact_decimal.h"
#include <ctime>

namespace om {
namespace {
std::string utc(uint64_t n) {
    if (!n) return "not supplied";
    const auto t = static_cast<time_t>(n);
    tm g{};
#if defined(_WIN32)
    if (gmtime_s(&g, &t)) return "unrepresentable UTC";
#else
    if (!gmtime_r(&t, &g)) return "unrepresentable UTC";
#endif
    char b[32];
    if (!strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", &g)) return "unrepresentable UTC";
    return b;
}
std::string display(const ExactDecimal &d) {
    std::string text;
    if (!d.formatRatio(1, 8, &text)) return "N/A (arithmetic capacity)";
    return text == "0.00000000" && !d.zero() ? "<0.00000001" : text;
}
}  // namespace

ManualMarketProjector makeManualMarketProjector(const ManualMarketPreferences &p, uint64_t startUtc, uint64_t startMono) {
    std::string usdt, fx, usd;
    bool valid = manualMarketValid(p) && startUtc > 0 && startUtc <= kManualLastUtc && startUtc >= p.confirmedUtc &&
        startUtc >= p.prlAsOfUtc && (!p.fxEnabled || startUtc >= p.fxAsOfUtc);
    ExactDecimal token, conversion, product;
    if (p.enabled && valid) {
        valid = ExactDecimal::parse(p.prlUsdt, &token);
        if (valid) usdt = display(token);
        if (p.fxEnabled) {
            valid = valid && ExactDecimal::parse(p.usdtUsd, &conversion) && token.multiply(conversion, &product);
            if (valid) { fx = display(conversion); usd = display(product); }
        }
    }
    const auto tokenStamp = utc(p.prlAsOfUtc), fxStamp = utc(p.fxAsOfUtc), confirmationStamp = utc(p.confirmedUtc);
    // Single terminal owner. Once a clock fault is observed, time catching up cannot revive this declaration.
    return [p, startUtc, startMono, valid, usdt, fx, usd, tokenStamp, fxStamp, confirmationStamp,
            lastUtc = startUtc, lastMono = startMono, clockFault = false](uint64_t nowUtc, uint64_t mono) mutable {
        PublicMarketView view;
        if (!p.enabled) return view;
        view.enabled = true;
        view.heading = "MANUAL/OFFLINE PRICE REFERENCES";
        view.lines.push_back("USER DECLARATION - NOT LIVE / no provider requests");
        if (!valid) {
            view.lines.push_back("N/A: invalid settings/UTC clock at activation; explicitly review again.");
            return view;
        }
        clockFault = clockFault || !nowUtc || nowUtc > kManualLastUtc || mono < lastMono || nowUtc < lastUtc;
        if (!clockFault) {
            const auto elapsedSeconds = (mono - startMono) / 1000, wall = nowUtc - startUtc;
            const auto drift = wall > elapsedSeconds ? wall - elapsedSeconds : elapsedSeconds - wall;
            clockFault = drift > 5;
        }
        if (clockFault) {
            view.lines.push_back("N/A: UTC/monotonic clock mismatch; explicitly review again. No quote was refreshed.");
            return view;
        }
        lastUtc = nowUtc;
        lastMono = mono;
        const auto elapsed = mono - startMono;
        auto age = [&](uint64_t reference) {
            const uint64_t base = (startUtc - reference) * 1000;
            return elapsed > UINT64_MAX - base ? UINT64_MAX : base + elapsed;
        };
        const auto tokenAge = age(p.prlAsOfUtc), fxAge = p.fxEnabled ? age(p.fxAsOfUtc) : 0;
        const bool tokenFresh = tokenAge <= static_cast<uint64_t>(p.validitySeconds) * 1000;
        const bool fxFresh = p.fxEnabled && fxAge <= static_cast<uint64_t>(p.validitySeconds) * 1000;
        const auto skew = p.prlAsOfUtc > p.fxAsOfUtc ? p.prlAsOfUtc - p.fxAsOfUtc : p.fxAsOfUtc - p.prlAsOfUtc;
        view.lines.push_back("Pearl mainnet PRL / USDT: " + (tokenFresh ? usdt + " USDT/PRL" : "N/A (stale manual input)") +
                             " | age " + std::to_string(tokenAge / 1000) + "s");
        view.lines.push_back("PRL reference UTC: " + tokenStamp);
        view.lines.push_back(std::string("FX USD per USDT: ") + (fxFresh ? fx + " USD/USDT" :
                             p.fxEnabled ? "N/A (stale manual input)" : "N/A (not configured; no assumed peg)"));
        if (p.fxEnabled) view.lines.push_back("FX reference UTC: " + fxStamp + " | age " + std::to_string(fxAge / 1000) + "s");
        view.lines.push_back("PRL/USD reference: " + (tokenFresh && fxFresh && skew <= p.maxFxSkewSeconds ? usd + " USD/PRL" :
                             tokenFresh && fxFresh ? "N/A (reference-time skew exceeds selected limit)" :
                             "N/A (needs valid PRL input and independent FX)"));
        view.lines.push_back("1h equal-time mean: N/A (no complete time series)");
        view.lines.push_back("Confirmed UTC: " + confirmationStamp + " (not a market event timestamp)");
        view.lines.push_back("Validity: " + std::to_string(p.validitySeconds) + "s | maximum FX reference skew: " +
                             std::to_string(p.maxFxSkewSeconds) + "s");
        view.lines.push_back("Manual references are not exchange quotes, credits or proceeds.");
        return view;
    };
}
}  // namespace om
