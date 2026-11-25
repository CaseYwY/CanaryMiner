// Source messages for the monthly billing-cycle editor.
#pragma once
#include "localization.h"

namespace om { namespace i18n {
enum class TariffMonthlyMessageKey { Toggle, Derive, Enabled, Day, RetainAmounts, Derived, Review,
    FeeToggle, FeeReview, FeeLabel, FeeOpeningRequired, ProratedExclusions,
    AnnualToggle, AnnualEnabled, AnnualChanged, AnnualMonth, AnnualReview,
    BimonthlyToggle, BimonthlyEnabled, BimonthlyChanged, BimonthlyMonth, BimonthlyReview, MonthlyAmount,
    SeasonalToggle, SeasonalEnabled, SeasonalReview, SeasonalConflict, SeasonalOpening, SeasonalBand,
    SeasonalOpeningPoint, Count };

inline LocalizedMessage tariffMonthlyMessage(std::string_view locale,TariffMonthlyMessageKey key) {
    static constexpr std::array<std::string_view,static_cast<size_t>(TariffMonthlyMessageKey::Count)> messages = {{
        "[M] Cycle policy", "[G] Derive cycle",
        "Enter the reset day and check the reset clock. Use G to derive dates.",
        "Day 1-31 at 00:00. Short months use their last day.",
        "Clear both opening amounts before changing the cycle start.",
        "Draft cycle dates derived. Review before saving.",
        "Monthly reset at 00:00 in the declared reset clock. Draft dates only.",
        "[F] Daily fee rule",
        "Daily fees use the civil-midnight rate, split by minutes in each billing cycle.",
        "fees prorated",
        "Re-enter the accrued subtotal using the selected daily fee rule.",
        "EXCLUDES demand/export/prepaid/independent resets/other proration/ordered taxes/unsupported rounding.",
        "[Y] Annual reset",
        "Enter the annual month and day. Derive dates with G and re-enter both opening amounts.",
        "Annual policy removed. Review the cycle dates and re-enter both opening amounts.",
        "Month 1-12 in the declared reset clock.",
        "Annual reset at the selected date in the declared reset clock. Usage carries across months.",
        "[B] Two-month reset",
        "Enter the start month and reset day. Use G to derive dates and re-enter both opening amounts.",
        "Two-month policy removed. Review the dates, tariff amounts and opening.",
        "Enter 1 for odd start months or 2 for even start months.",
        "Two-month bill uses twice the monthly charge at the cycle's monthly-average usage.",
        "Enter the monthly value for this two-month rule.",
        "[A] Seasonal allowance",
        "Enter the publication time and add seasonal start dates in calendar order.",
        "The allowance sums each tariff day's quota for the full cycle. Clear both band bounds.",
        "Choose either the two-month rule or seasonal allowance.",
        "The allowance changed. Re-enter the accrued subtotal.",
        "Clear both bounds. The first band's limit comes from the seasonal allowance.",
        "The opening changed. Re-enter the import and accrued subtotal."
    }};
    const auto index=static_cast<size_t>(key);
    if(index>=messages.size())return {};
    return {messages[index],"en",ReviewStatus::Source,locale!="en",false};
}

inline std::string tariffMonthlyEnglish(TariffMonthlyMessageKey key) {
    return std::string(tariffMonthlyMessage("en",key).text);
}
} } // namespace om::i18n
