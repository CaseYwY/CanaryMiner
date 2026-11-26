#include "meter_prepaid.h"
#include "meter_bill_input.h"
#include "hash.h"

#include <algorithm>

namespace om::meter_bill_detail {
namespace {

constexpr size_t rootFields=10, maxBands=32, maxPurchases=256;
constexpr int maxFeeDays=maxTariffComparisonCycles * 370 + 2;
constexpr const char *arithError="prepaid amounts exceed arithmetic capacity";

bool readScaled(const JsonValue *input,ElectricityAmount &out) {
    ExactDecimal value;
    if (!nonnegativeAmount(input,value)) return false;
    const auto scaled=electricityKwh(value);
    if (!scaled) return false;
    out=*scaled;
    return true;
}

void sortKeys(JsonValue &value) {
    if (value.isArray()) {
        for (auto &item:value.items) sortKeys(item);
    } else if (value.isObject()) {
        std::sort(value.members.begin(),value.members.end(),[](const auto &a,const auto &b) {
            return a.first<b.first;
        });
        for (auto &member:value.members) sortKeys(member.second);
    }
}

bool add(ElectricityAmount &target,const ElectricityAmount &item) {
    return target.numerator.add(item.numerator,&target.numerator);
}

const char *accrueFee(const PrepaidReviewInput &input,int64_t begin,int64_t end,ElectricityAmount &unpaid,
                      const std::function<bool()> &stopped) {
    if (input.feePerDay.zero()) return nullptr;
    int days=0;
    for (auto cursor=begin;cursor<end;) {
        if (stopped()) return "review cancelled";
        const auto day=civilDayAt(input.feeClock,cursor);
        if (day.status!=CivilStatus::Ok || cursor<day.beginUtcMs || cursor>=day.endUtcMs ||
            !civilDayIsContiguous(input.feeClock,day))
            return "prepaid fee clock has an unavailable or disjoint day";
        if (day.endUtcMs>end) break;
        if (++days>maxFeeDays) return "prepaid fee span exceeds calendar capacity";
        cursor=day.endUtcMs;
    }
    ExactDecimal count, fee;
    if (!ExactDecimal::parse(std::to_string(days),&count) || !input.feePerDay.multiply(count,&fee)) return arithError;
    const auto amount=electricityKwh(fee);
    return amount && add(unpaid,*amount) ? nullptr : arithError;
}

bool appendAmount(std::string &report,const char *label,const ElectricityAmount &value,const std::string &unit) {
    std::string amount;
    if (!value.format(6,&amount)) return false;
    report+="  "+std::string(label)+" "+amount+" "+unit+"\n";
    return true;
}

}  // namespace

const char *decodePrepaidReview(const std::string &text,const TariffDeclaration &tariff,int64_t cutoff,
                               int64_t meterEnd,PrepaidReviewInput &out) {
    JsonValue root;
    uint64_t version=0, monthlyDay=0;
    int64_t published=0;
    if (!parseJsonStrict(text,&root,nullptr,{maxPrepaidFileBytes,rootFields}) || !root.isObject() || root.members.size()!=rootFields)
        return "invalid prepaid review document";
    const auto *schema=root.find("schema_version");
    const auto *scope=root.find("scope"), *currency=root.find("currency");
    const auto *reset=root.find("purchase_reset"), *feeClock=root.find("fee_clock");
    const auto *day=reset ? reset->find("monthly_day") : nullptr;
    const auto *resetClock=reset ? reset->find("clock") : nullptr;
    const auto *opening=root.find("opening"), *bands=root.find("bands"), *purchases=root.find("purchases");
    if (!schema || !schema->asUint64(&version) || version!=1 || !scope || !scope->isString() ||
        !currency || !currency->isString() || !instant(root.find("published_utc_ms"),published,true) ||
        !reset || !reset->isObject() || reset->members.size()!=2 || !day || !day->asUint64(&monthlyDay) ||
        monthlyDay<1 || monthlyDay>31 || !resetClock || !feeClock || !opening || !opening->isObject() ||
        opening->members.size()!=4 || !bands || !bands->isArray() || bands->items.empty() ||
        bands->items.size()>maxBands || !purchases || !purchases->isArray() || purchases->items.size()>maxPurchases)
        return "invalid prepaid review document";
    if (scope->text!=tariff.opening.evidence.scope || currency->text!=tariff.plan.currency)
        return "prepaid scope or currency differs from the saved tariff";
    if (published>cutoff) return "prepaid rule is published after the review cutoff";
    if ((usesNamedClock(resetClock) || usesNamedClock(feeClock)) && configureBundledCivilTimeData()!=CivilStatus::Ok)
        return "reviewed prepaid time database unavailable or invalid";

    PrepaidReviewInput candidate;
    candidate.currency=currency->text;
    candidate.publishedUtcMs=published;
    candidate.resetDay=static_cast<unsigned>(monthlyDay);
    if (!decodeTariffCivilClock(*resetClock,candidate.resetClock) ||
        !decodeTariffCivilClock(*feeClock,candidate.feeClock) ||
        !nonnegativeAmount(root.find("fee_per_civil_day"),candidate.feePerDay))
        return "invalid prepaid reset or daily-fee rule";
    if (!instant(opening->find("through_utc_ms"),candidate.throughUtcMs,true) ||
        candidate.throughUtcMs!=tariff.opening.throughUtcMs ||
        !readScaled(opening->find("purchased_in_cycle_kwh"),candidate.openingPurchased) ||
        !readScaled(opening->find("token_inventory_kwh"),candidate.inventory) ||
        !readScaled(opening->find("unpaid_fee"),candidate.unpaidFee))
        return "invalid prepaid opening or through-time differs from the saved meter opening";

    ExactDecimal lower;
    for (size_t i=0;i<bands->items.size();++i) {
        const auto &row=bands->items[i];
        const auto *threshold=row.find("through_kwh");
        TariffBand band;
        if (!row.isObject() || row.members.size()!=2 || !threshold ||
            !nonnegativeAmount(row.find("rate_per_kwh"),band.ratePerKwh))
            return "invalid progressive prepaid purchase band";
        if (i+1==bands->items.size()) {
            if (threshold->type!=JsonValue::Type::Null) return "prepaid purchase bands require an unbounded final band";
        } else {
            auto &upper=band.throughKwh.emplace();
            if (!nonnegativeAmount(threshold,upper) || upper.compare(lower)<=0 || !electricityKwh(upper))
                return "prepaid purchase thresholds must strictly increase";
            lower=upper;
        }
        candidate.bands.push_back(std::move(band));
    }

    auto previous=candidate.throughUtcMs;
    for (const auto &row:purchases->items) {
        PrepaidPurchase purchase;
        if (!row.isObject() || row.members.size()!=3 || !instant(row.find("utc_ms"),purchase.utcMs) ||
            !readScaled(row.find("kwh"),purchase.quantity) || purchase.quantity.numerator.zero() ||
            !readScaled(row.find("cash_paid"),purchase.cashPaid))
            return "invalid prepaid purchase";
        if (purchase.utcMs<=previous || purchase.utcMs>meterEnd)
            return "prepaid purchases must strictly increase within the observed meter coverage";
        candidate.purchases.push_back(std::move(purchase));
        previous=candidate.purchases.back().utcMs;
    }

    root.members.erase(std::remove_if(root.members.begin(),root.members.end(),[](const auto &entry){
        return entry.first=="purchases";
    }),root.members.end());
    sortKeys(root);
    candidate.openingBinding=sha256Hex(tariffJson(root));
    out=std::move(candidate);
    return nullptr;
}

const char *reviewPrepaid(const PrepaidReviewInput &input,const std::vector<MeterImportInterval> &series,int64_t cutoff,
                          const std::function<bool()> &stopped,std::string &report,std::string &exact) {
    if (input.publishedUtcMs>cutoff) return "prepaid rule is published after the review cutoff";
    if (series.empty() || series.front().beginUtcMs!=input.throughUtcMs) return "invalid prepaid meter coverage";
    TariffCycleBounds cycle;
    if (!tariffMonthlyCycleAt(input.resetDay,input.resetClock,input.throughUtcMs,cycle))
        return "prepaid purchase reset clock is unavailable";
    auto inventory=input.inventory, unpaid=input.unpaidFee, purchased=input.openingPurchased;
    ElectricityAmount energyTotal, feeTotal, paidTotal, acquired, consumed;
    auto feeThrough=input.throughUtcMs;
    size_t next=0;
    std::string purchaseProof;
    const auto &currency=input.currency;
    report+="Prepaid purchase review (separate from supply charges)\n";
    for (const auto &interval:series) {
        if (stopped()) return "review cancelled";
        const auto at=interval.endUtcMs;
        if (next<input.purchases.size() && input.purchases[next].utcMs<at)
            return "meter register needs a reading at each prepaid purchase";
        if (!inventory.numerator.subtract(interval.importedKwh.numerator,&inventory.numerator) ||
            !add(consumed,interval.importedKwh)) return arithError;
        if (inventory.numerator.negative()) return "prepaid inventory does not cover meter consumption before a purchase";
        while (at>=cycle.endUtcMs) {
            if (stopped()) return "review cancelled";
            const auto boundary=cycle.endUtcMs;
            if (!tariffMonthlyCycleAt(input.resetDay,input.resetClock,boundary,cycle) ||
                cycle.beginUtcMs!=boundary || cycle.endUtcMs<=boundary)
                return "prepaid purchase reset clock is unavailable";
            purchased={};
        }
        if (next==input.purchases.size() || input.purchases[next].utcMs!=at) continue;

        const auto &purchase=input.purchases[next++];
        const auto oldPurchased=purchased;
        ElectricityAmount energy;
        if (!add(purchased,purchase.quantity) ||
            !progressiveTierChange(input.bands,oldPurchased.numerator,purchased.numerator,energy.numerator) ||
            !add(inventory,purchase.quantity) || !add(acquired,purchase.quantity))
            return arithError;
        if (const auto *problem=accrueFee(input,feeThrough,at,unpaid,stopped)) return problem;
        feeThrough=at;
        if (!add(energyTotal,energy) || !add(feeTotal,unpaid) || !add(paidTotal,purchase.cashPaid)) return arithError;
        report+=" Purchase at "+std::to_string(at)+" UTC ms\n";
        if (!appendAmount(report,"Energy acquired",purchase.quantity,"kWh") ||
            !appendAmount(report,"Modeled energy acquisition charge",energy,currency) ||
            !appendAmount(report,"Daily fee collected in model",unpaid,currency) ||
            !appendAmount(report,"Declared cash paid",purchase.cashPaid,currency)) return arithError;
        purchaseProof+=std::to_string(at)+" "+purchase.quantity.numerator.text()+" "+
                       purchase.cashPaid.numerator.text()+" "+energy.numerator.text()+" "+unpaid.numerator.text()+"\n";
        unpaid={};
    }
    if (const auto *problem=accrueFee(input,feeThrough,series.back().endUtcMs,unpaid,stopped)) return problem;
    ElectricityAmount due, difference;
    if (!energyTotal.numerator.add(feeTotal.numerator,&due.numerator) ||
        !paidTotal.numerator.subtract(due.numerator,&difference.numerator)) return arithError;
    report+=" Prepaid totals for reviewed meter coverage\n";
    if (!appendAmount(report,"Acquired energy",acquired,"kWh") || !appendAmount(report,"Consumed meter energy",consumed,"kWh") ||
        !appendAmount(report,"Modeled energy acquisition total",energyTotal,currency) ||
        !appendAmount(report,"Modeled daily fee collected total",feeTotal,currency) ||
        !appendAmount(report,"Modeled purchase total",due,currency) ||
        !appendAmount(report,"Declared cash paid total",paidTotal,currency) ||
        !appendAmount(report,"Cash paid minus modeled purchase total",difference,currency) ||
        !appendAmount(report,"Unpaid daily fee",unpaid,currency) || !appendAmount(report,"Token inventory remaining",inventory,"kWh"))
        return arithError;
    report+="  Purchase period ["+std::to_string(cycle.beginUtcMs)+", "+std::to_string(cycle.endUtcMs)+") UTC ms\n";
    if (!appendAmount(report,"Purchased in current purchase period",purchased,"kWh")) return arithError;

    exact+="[\"prepaid\",\""+input.openingBinding+"\",\""+sha256Hex(purchaseProof)+"\","+
           std::to_string(cycle.beginUtcMs)+","+std::to_string(cycle.endUtcMs);
    for (const auto *field:{&purchased,&inventory,&unpaid,&acquired,&consumed,&energyTotal,&feeTotal,&paidTotal})
        exact+=",\""+field->numerator.text()+"\"";
    exact+="]\n";
    return nullptr;
}

}  // namespace om::meter_bill_detail
