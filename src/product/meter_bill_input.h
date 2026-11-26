#pragma once

#include "exact_decimal.h"
#include "json_dom.h"
#include "meter_bill.h"

namespace om::meter_bill_detail {

inline bool instant(const JsonValue *value,int64_t &result,bool allowEpoch=false) {
    uint64_t number=0;
    if (!value || !value->asUint64(&number) || (!allowEpoch && !number) || number>meterBillMaxUtcMs) return false;
    result=static_cast<int64_t>(number);
    return true;
}

inline bool signedAmount(const JsonValue *value,ExactDecimal &amount) {
    return value && value->isString() && ExactDecimal::parse(value->text,&amount) &&
           amount.text()==value->text && amount.digits()<=38 && amount.scale()<=18;
}

inline bool nonnegativeAmount(const JsonValue *value,ExactDecimal &amount) {
    return signedAmount(value,amount) && !amount.negative();
}

inline bool usesNamedClock(const JsonValue *clock) {
    const auto *kind=clock ? clock->find("kind") : nullptr;
    return kind && kind->isString() && kind->text=="named_iana";
}

}  // namespace om::meter_bill_detail
