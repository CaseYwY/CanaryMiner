// Read-only display preferences and financial semantics; synthetic inputs, no network/GPU.
#include <limits>
#include <string>

#include "../src/product/settings.h"
#include "product_test_util.h"

using namespace om;

static bool has(const std::string &text, const char *needle) { return text.find(needle) != std::string::npos; }
static std::string lines(const FinancialView &view) {
    std::string result;
    for (const auto &line : financialViewLines(view)) result += line + "\n";
    return result;
}

static void financeTests() {
    FinancialView value;
    CHECK(financialViewLines(value).empty());
    value.enabled = true;
    value.market.fresh = true;
    value.market.source = "synthetic";
    value.market.latestTokenUsdt = 3;
    value.market.latestUsdtUsd = 1;
    value.market.averageTokenUsd1h = 2.5;
    value.todayForecast = {20, true};
    value.actual24h = {10, true};
    value.actualCumulative = {15, true};
    CHECK(productionUsdNow(value.actualCumulative, value.market) == 45);
    CHECK(has(lines(value), "Actual credited cumulative: 15.000000 PRL | at latest price $45.00"));
    CHECK(!has(lines(value), "Estimated net"));
    CHECK(has(lines(value), "sales and current holdings are unknown"));
    value.market.latestTokenUsdt = 4;
    CHECK(productionUsdNow(value.actualCumulative, value.market) == 60);
    CHECK(value.actualCumulative.tokens == 15);
    value.market.latestUsdtUsd = .5;
    CHECK(latestTokenPriceUsd(value.market) == 2);
    CHECK(productionUsdNow(value.actualCumulative, value.market) == 30);
    value.market.latestUsdtUsd.reset();
    CHECK(!productionUsdNow(value.actualCumulative, value.market));
    CHECK(has(lines(value), "Latest token/USDT: 4.000000"));
    CHECK(has(lines(value), "Actual credited cumulative: 15.000000 PRL | at latest price N/A"));
    value.market.latestUsdtUsd = 1;
    value.electricityConfigured = true;
    CHECK(has(lines(value), "Estimated net today USD: N/A"));
    value.electricityTodayUsd = 3;
    value.electricityHorizonMatched = true;
    CHECK(has(lines(value), "Estimated net today USD: $77.00"));
    value.electricityTodayUsd = 100;
    CHECK(has(lines(value), "Estimated net today USD: $-20.00"));
    value.electricityTodayUsd = -3;
    CHECK(has(lines(value), "Estimated net today USD: $83.00"));
    value.electricityTodayUsd = 0;
    CHECK(has(lines(value), "Estimated net today USD: $80.00"));
    value.energyBasis = FinancialEnergyBasis::WholeSystem;
    CHECK(has(lines(value), "whole-system estimate; not isolated mining increment"));
    value.energyBasis = FinancialEnergyBasis::IncrementalWall;
    CHECK(has(lines(value), "matched incremental-wall estimate"));
    value.energyBasis = static_cast<FinancialEnergyBasis>(99);
    CHECK(has(lines(value), "Estimated net today USD: N/A"));
    value.energyBasis = FinancialEnergyBasis::GpuBoardProxy;
    value.todayForecast.complete = false;
    CHECK(has(lines(value), "[partial coverage]"));
    CHECK(has(lines(value), "Estimated net today USD: N/A"));
    value.todayForecast.complete = true;
    value.market.fresh = false;
    CHECK(!latestTokenPriceUsd(value.market));
    CHECK(has(lines(value), "15.000000 PRL | at latest price N/A"));
    value.market.fresh = true;
    value.market.latestTokenUsdt = std::numeric_limits<double>::infinity();
    CHECK(!latestTokenPriceUsd(value.market));
    CHECK(!has(lines(value), "$inf"));
    value.market.latestTokenUsdt = -1;
    CHECK(!latestTokenPriceUsd(value.market));
    value.market.latestTokenUsdt = 1e308;
    value.market.latestUsdtUsd = 1e308;
    CHECK(!latestTokenPriceUsd(value.market));
    value.actual24h.tokens.reset();
    CHECK(has(lines(value), "Actual credited 24h: N/A"));
}

static void displayTests() {
    DashModel model;
    DashGpu gpu;
    gpu.name = "Synthetic device";
    gpu.pciBus = "0000:01:00.0";
    gpu.poolState = "synthetic-pool-state";
    gpu.restarts = 7;
    gpu.state = "Error";
    gpu.detail = "synthetic-private-detail";
    model.gpus.push_back(gpu);
    DashView view;
    view.columns = 120;
    view.options.detailsEnabled = false;
    view.options.logEnabled = false;
    CHECK(!handleDashKey(view, 'd'));
    CHECK(!handleDashKey(view, 'l'));
    CHECK(!has(renderDashboard(model, view), "[D] Details"));
    CHECK(!has(renderDashboard(model, view), "[L] Log"));
    view.page = DashPage::Log;
    renderDashboard(model, view);
    CHECK(view.page == DashPage::Main);
    view.options.detailsEnabled = true;
    view.options.detailsIdentity = view.options.detailsPool = view.options.detailsRestarts = view.options.detailsErrors = false;
    CHECK(handleDashKey(view, 'd'));
    const auto detail = renderDashboard(model, view);
    CHECK(!has(detail, gpu.name.c_str()));
    CHECK(!has(detail, gpu.pciBus.c_str()));
    CHECK(!has(detail, gpu.poolState.c_str()));
    CHECK(!has(detail, gpu.detail.c_str()));
    CHECK(has(detail, "Error"));  // global safety/state is never hidden by detail-field switches
    view.options.logEnabled = true;
    view.page = DashPage::Log;
    model.logEntries = {{"information", "01:02:03", "2000-01-01T01:02:03Z", DashLogSeverity::Info},
                        {"warning", "01:02:04", "2000-01-01T01:02:04Z", DashLogSeverity::Warning},
                        {"failure", "01:02:05", "2000-01-01T01:02:05Z", DashLogSeverity::Error},
                        {"raw-text-warning", "01:02:06", "2000-01-01T01:02:06Z", DashLogSeverity::Unclassified}};
    view.options.logSeverity = 2;
    view.options.logUnclassified = false;
    auto text = renderDashboard(model, view);
    CHECK(has(text, "failure") && !has(text, "information") && !has(text, "warning"));
    view.options.logSeverity = 1;
    view.options.logTimestamps = view.options.logIsoTime = true;
    text = renderDashboard(model, view);
    CHECK(has(text, "warning") && !has(text, "raw-text-warning"));
    CHECK(has(text, "2000-01-01T01:02:04Z"));
    view.options.logUnclassified = true;
    view.options.logFollow = false;
    text = renderDashboard(model, view);
    CHECK(has(text, "raw-text-warning") && has(text, "Frozen"));
    model.logEntries.push_back({"added-later", "", "", DashLogSeverity::Error});
    CHECK(!has(renderDashboard(model, view), "added-later"));
    view.options.logFollow = true;
    CHECK(has(renderDashboard(model, view), "added-later"));
    model.logEntries.clear();
    for (int i = 0; i < 40; ++i) model.logEntries.push_back({"record-" + std::to_string(100 + i), "", "", DashLogSeverity::Error});
    view.options.logRows = 25;
    text = renderDashboard(model, view);
    CHECK(!has(text, "record-100") && has(text, "record-139"));
    view.rows = 12;
    renderDashboard(model, view);
    CHECK(view.scroll > 0);
    CHECK(handleDashKey(view, kKeyUp) && view.logScrollHeld);
    CHECK(handleDashKey(view, 'f') && !view.logScrollHeld);

    SettingsPanel panel;
    panel.open(UiPreferences{});
    panel.group = 9;
    panel.groupsFocused = false;
    panel.key(' ', {});
    CHECK(!panel.draft.detailsEnabled && panel.active.detailsEnabled);
    panel.group = 10;
    const char *labels[] = {"Log page", "Severity", "Include unclassified", "Records shown", "Show observed time", "Time format", "Follow new output"};
    for (int field = 0; field < panel.fields(); ++field) {
        panel.field = field;
        const auto before = panel.draft;
        panel.key(' ', {});
        CHECK(panel.draft != before);
        const auto frame = panel.render(40, 12, ColorLevel::None);
        CHECK(has(frame, (std::string("> ") + labels[field]).c_str()));
        CHECK(std::count(frame.begin(), frame.end(), '\n') < 12);
    }
    panel.key(kKeyF2, {});
    for (int i = 0; i < 20; ++i) panel.key(kKeyDown, {});
    CHECK(panel.reviewOffset > 0 && has(panel.render(40, 12, ColorLevel::None), "Log records"));
    UiPreferences parsed;
    std::string error;
    CHECK(parseUiPreferences(uiPreferencesJson(panel.draft), &parsed, &error) && parsed == panel.draft);
    CHECK(!parseUiPreferences("{\"schema_version\":1,\"log_rows\":25}", &parsed, &error));
    CHECK(!parseUiPreferences("{\"schema_version\":2,\"log_rows\":24}", &parsed, &error));
    CHECK(!parseUiPreferences("{\"schema_version\":2,\"log_severity\":3}", &parsed, &error));
}

int main() {
    financeTests();
    displayTests();
    return finishTests("product_display");
}
