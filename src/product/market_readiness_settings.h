// Read-only view of the reviewed public data contracts. No source is started here.
#pragma once
#include "dashboard.h"
#include "market_data.h"
#include "market_readiness_messages.h"

namespace om {
class MarketReadinessSettings {
   public:
    bool visible = false;

    void open() { visible = true; selected_ = MarketSeries::GatePearlMainnetUsdtV1; }

    void key(int key) {
        if (key == 27) visible = false;
        if (key == '\t' || key == kKeyBackTab || key == kKeyLeft || key == kKeyRight ||
            key == kKeyUp || key == kKeyDown) {
            selected_ = selected_ == MarketSeries::GatePearlMainnetUsdtV1 ?
                MarketSeries::KrakenUsdtUsdV1 : MarketSeries::GatePearlMainnetUsdtV1;
        }
    }

    std::string render(int columns, int rows, ColorLevel color) const {
        using namespace dash_detail;
        using Key = i18n::MarketReadinessMessageKey;
        using i18n::marketReadinessEnglish;
        const auto title = marketReadinessEnglish(Key::Title);
        if (columns < 60 || (rows && rows < 12))
            return title + "\n" + marketReadinessEnglish(Key::Resize) + "\n" + marketReadinessEnglish(Key::Back);
        const auto width = static_cast<size_t>(columns - 1);
        std::string frame = paint(title, Tone::Canary, color) + "\n";
        for (const auto key : {selected_ == MarketSeries::GatePearlMainnetUsdtV1 ? Key::Gate : Key::Kraken,
                               Key::Latest, selected_ == MarketSeries::GatePearlMainnetUsdtV1 ? Key::GateHour : Key::KrakenHour,
                               Key::Approval, Key::Reward}) {
            for (const auto &row : wrapText(marketReadinessEnglish(key), width)) frame += row + "\n";
        }
        return frame + marketReadinessEnglish(Key::Footer);
    }

   private:
    MarketSeries selected_ = MarketSeries::GatePearlMainnetUsdtV1;
};
}  // namespace om
