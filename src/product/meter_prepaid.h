// Offline reconciliation of local token purchases and measured meter consumption.
#pragma once

#include "tariff_declaration.h"

namespace om::meter_bill_detail {

inline constexpr size_t maxPrepaidFileBytes=64 * 1024;

struct PrepaidPurchase {
    int64_t utcMs=0;
    ElectricityAmount quantity, cashPaid;
};

struct PrepaidReviewInput {
    std::string currency, openingBinding;
    unsigned resetDay=0;
    CivilClock resetClock, feeClock;
    ExactDecimal feePerDay;
    int64_t throughUtcMs=0, publishedUtcMs=0;
    ElectricityAmount openingPurchased, inventory, unpaidFee;
    std::vector<TariffBand> bands;
    std::vector<PrepaidPurchase> purchases;
};

const char *decodePrepaidReview(const std::string &text,const TariffDeclaration &tariff,int64_t cutoff,
                               int64_t meterEnd,PrepaidReviewInput &out);
// The full or prefix series starts at the declared opening; same-instant purchases follow meter consumption.
const char *reviewPrepaid(const PrepaidReviewInput &input,const std::vector<MeterImportInterval> &series,int64_t cutoff,
                          const std::function<bool()> &stopped,std::string &report,std::string &exact);

}  // namespace om::meter_bill_detail
