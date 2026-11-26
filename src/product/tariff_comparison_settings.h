// Read-only scenario calculation from a saved tariff and user-entered power.
#pragma once

#include <array>
#include <memory>
#include <thread>
#include "dashboard.h"
#include "tariff_declaration.h"
#include "tariff_monthly_messages.h"
#include "tariff_comparison_messages.h"

namespace om {

class TariffComparisonSettings {
   public:
    bool visible = false;

    void open(const JsonValue &saved) {
        close();
        visible = true;
        TariffDeclaration checked;
        if (decodeTariffDeclaration(saved, &checked)) {
            declaration_ = std::move(checked);
            end_ = declaration_->opening.cycleEndUtcMs;
        }
    }

    void key(int key, uint64_t nowUtc) {
        if (key == 27) { close(); return; }
        if (!declaration_ || pending()) return;
        if (key == '\t' || key == kKeyUp || key == kKeyDown || key == kKeyBackTab) {
            focus_ = 1 - focus_;
        } else if (key == kKeyLeft || key == kKeyRight) {
            if (!declaration_->resetSchedule) return;
            auto cycles = cycles_;
            if (key == kKeyLeft && cycles > 1) --cycles;
            if (key == kKeyRight && cycles < maxTariffComparisonCycles) ++cycles;
            if (cycles != cycles_) {
                end_ = tariffComparisonEnd(*declaration_, cycles);
                cycles_ = cycles;
                clearResult();
            }
        } else if (key == '\r' || key == '\n') {
            calculate(nowUtc);
        } else if (key == 8 || key == 127) {
            auto &value = input_[focus_];
            if (!value.empty()) value.pop_back();
            clearResult();
        } else if (key >= 32 && key <= 255) {
            auto &value = input_[focus_];
            // Keep an invalid byte or overflow marker so a malformed answer cannot become a valid prefix.
            if (value.size() <= (apparentRule() ? inputLimit_ * 2 + 1 : inputLimit_)) value.push_back(key <= 126 ? static_cast<char>(key) : '?');
            clearResult();
        }
    }

    void requestStop() {
        if (job_) job_->cancel.store(true, std::memory_order_relaxed);
    }

    void close() {
        requestStop();
        // Retain the one worker slot. Closing the page must not join a running calculation.
        visible = false;
        declaration_.reset();
        end_.reset();
        cycles_ = 1;
        focus_ = 0;
        input_ = {};
        notice_.clear();
    }

    std::string render(int columns, int rows, ColorLevel color) const {
        using namespace dash_detail;
        using i18n::tariffComparisonEnglish;
        if ((rows && rows < 12) || columns < 60)
            return tariffComparisonEnglish(Key::ShortTitle) + "\n" + tariffComparisonEnglish(Key::Resize) + "\n" + tariffComparisonEnglish(Key::Back);
        const auto width = static_cast<size_t>(columns - 1);
        std::string out = paint(tariffComparisonEnglish(Key::Title), Tone::Canary, color) + "\n";
        if (!declaration_) {
            return out + tariffComparisonEnglish(Key::NeedDeclaration) + "\n" + tariffComparisonEnglish(Key::Back);
        }
        const auto &o = declaration_->opening;
        auto add = [&](const std::string &line) {
            for (const auto &part : wrapText(line, width)) out += part + "\n";
        };
        add(end_ ? tariffComparisonEnglish(Key::Span, {{"begin", std::to_string(o.throughUtcMs)}, {"end", std::to_string(*end_)}})
                 : tariffComparisonEnglish(Key::EndUnavailable));
        const bool net=declaration_->plan.exportPayment.has_value();
        auto cycleLabel=declaration_->resetSchedule
            ? tariffComparisonEnglish(net?Key::NetCycles:Key::Cycles, {{"count", std::to_string(cycles_)}, {"max", std::to_string(maxTariffComparisonCycles)}})
            : tariffComparisonEnglish(net?Key::NetInstruction:Key::Instruction);
        if(declaration_->dailyFeeAllocation()==DailyFeeAllocation::ProrateByCycleMinutes)
            cycleLabel+=" | "+i18n::tariffMonthlyEnglish(i18n::TariffMonthlyMessageKey::FeeLabel);
        add(cycleLabel);
        if (const auto *rule = apparentRule()) {
            add(std::string(focus_ == 0 ? "> " : "  ") + tariffComparisonEnglish(Key::WithoutApparent, {{"power", input_[0]}}));
            add(std::string(focus_ == 1 ? "> " : "  ") + tariffComparisonEnglish(Key::WithApparent, {{"power", input_[1]}}));
            if (!job_ && notice_.empty()) add(tariffComparisonEnglish(rule->powerFactorVaPlaces ? Key::ApparentFactorInput : Key::ApparentInput));
        } else {
            add(std::string(focus_ == 0 ? "> " : "  ") + tariffComparisonEnglish(net?Key::WithoutNetWatts:Key::WithoutWatts, {{"watts", input_[0]}}));
            add(std::string(focus_ == 1 ? "> " : "  ") + tariffComparisonEnglish(net?Key::WithNetWatts:Key::WithWatts, {{"watts", input_[1]}}));
        }
        if (pending()) add(tariffComparisonEnglish(Key::Pending));
        else if (job_ && !job_->cancel.load(std::memory_order_relaxed)) {
            if (job_->results.empty()) add(tariffComparisonEnglish(Key::Failed));
            else for (const auto &line : job_->results) add(line);
        } else if (!notice_.empty()) add(notice_);
        // No final LF. At 60 x 12 all 12 rows may already be in use.
        out += tariffComparisonEnglish(Key::Footer);
        return out;
    }

   private:
    using Key = i18n::TariffComparisonMessageKey;
    struct Job {
        std::atomic<bool> cancel{false}, done{false};
        std::thread worker;
        std::vector<std::string> results;
        ~Job() {
            cancel.store(true, std::memory_order_relaxed);
            if (worker.joinable()) worker.join();
        }
    };

    static constexpr size_t inputLimit_ = 20;
    static constexpr size_t amountTextLimit_ = 48;  // at most two lines per result at 60 columns
    std::optional<TariffDeclaration> declaration_;
    std::optional<int64_t> end_;
    unsigned cycles_ = 1;
    size_t focus_ = 0;
    std::array<std::string, 2> input_;
    std::string notice_;
    std::shared_ptr<Job> job_;

    const DemandChargeRule *apparentRule() const {
        if (!declaration_ || !declaration_->plan.demandCharge || !declaration_->plan.demandCharge->apparent())
            return nullptr;
        return &*declaration_->plan.demandCharge;
    }

    static bool parsePower(const std::string &text, ExactDecimal &value, bool signedNet=false) {
        if (text.empty() || text.size()>inputLimit_) return false;
        const bool plus=signedNet&&text.front()=='+';
        const size_t first=(plus||(signedNet&&text.front()=='-'))?1:0;
        if (text.find_first_not_of("0123456789.",first)!=std::string::npos) return false;
        return ExactDecimal::parse(plus?text.substr(1):text,&value);
    }

    bool busy() const { return job_ && !job_->done.load(std::memory_order_acquire); }
    bool pending() const { return busy() && !job_->cancel.load(std::memory_order_relaxed); }
    void clearResult() {
        if (!busy()) job_.reset();
        notice_.clear();
    }

    void calculate(uint64_t nowUtc) {
        using i18n::tariffComparisonEnglish;
        if (busy()) {
            notice_ = tariffComparisonEnglish(Key::RetryAfterCancel);
            return;
        }
        clearResult();
        if (!end_) {
            notice_ = tariffComparisonEnglish(Key::EndUnavailable);
            return;
        }
        std::array<ExactDecimal, 2> watts;
        std::array<std::optional<ExactDecimal>, 2> apparentVa;
        const auto *rule = apparentRule();
        const bool net=declaration_->plan.exportPayment.has_value();
        for (size_t i = 0; i < watts.size(); ++i) {
            const auto &value = input_[i];
            if (!rule) {
                if (parsePower(value,watts[i],net)) continue;
                notice_ = tariffComparisonEnglish(net?Key::NeedNetWatts:Key::NeedWatts);
                return;
            }
            const auto separator = value.find_first_of("/@");
            ExactDecimal inputPower, va;
            if (separator == std::string::npos || !parsePower(value.substr(0, separator), watts[i]) ||
                !parsePower(value.substr(separator + 1), inputPower)) {
                notice_ = tariffComparisonEnglish(rule->powerFactorVaPlaces ? Key::NeedApparentFactor : Key::NeedApparent);
                return;
            }
            if (value[separator] == '/' && inputPower.compare(watts[i]) >= 0) va = std::move(inputPower);
            else if (value[separator] != '@' || !apparentDemandFromPowerFactor(*rule, watts[i], inputPower, va)) {
                notice_ = tariffComparisonEnglish(rule->powerFactorVaPlaces ? Key::NeedApparentFactor : Key::NeedApparent);
                return;
            }
            apparentVa[i] = std::move(va);
        }
        const auto &d = *declaration_;
        if (!nowUtc || nowUtc < d.confirmedUtc || nowUtc > 253402300799ULL) {
            notice_ = tariffComparisonEnglish(Key::Clock);
            return;
        }
        try {
            auto owned = std::make_shared<Job>();
            auto *job = owned.get();
            // The worker owns an immutable copy of the reviewed inputs and original decision time.
            job->worker = std::thread([job, d, watts, apparentVa, cycles = cycles_, decision = static_cast<int64_t>(nowUtc * 1000)] {
                try {
                    auto result = evaluate(d, watts, apparentVa, cycles, decision, job->cancel);
                    if (!job->cancel.load(std::memory_order_relaxed)) job->results = std::move(result);
                } catch (...) {}  // No partial comparison is published if evaluation fails.
                job->done.store(true, std::memory_order_release);
            });
            job_ = std::move(owned);
        } catch (...) {
            notice_ = tariffComparisonEnglish(Key::Failed);
        }
    }

    static std::vector<std::string> evaluate(const TariffDeclaration &d, const std::array<ExactDecimal, 2> &watts,
                                             const std::array<std::optional<ExactDecimal>, 2> &apparentVa,
                                             unsigned cycles, int64_t decision, const std::atomic<bool> &cancel) {
        using i18n::tariffComparisonEnglish;
        const auto result = compareTariffCycles(d, watts, cycles, decision, &cancel, apparentVa);
        if (result.status != ElectricityStatus::Ok) {
            const auto cycle = std::to_string(result.failedCycle);
            if (result.splitsChargedDay) return {tariffComparisonEnglish(Key::ChargedDay, {{"cycle", cycle}})};
            const auto status = electricityStatusName(result.status);
            if (cycles > 1 && result.failedCycle)
                return {tariffComparisonEnglish(Key::CycleStatus, {{"cycle", cycle}, {"status", status}})};
            return {tariffComparisonEnglish(Key::ModelStatus, {{"status", status}})};
        }
        std::string baseCharge, withCharge, change;
        if (!result.withoutMining.format(6, &baseCharge) ||
            !result.withMining.format(6, &withCharge) ||
            !result.difference.format(6, &change) ||
            baseCharge.size() > amountTextLimit_ || withCharge.size() > amountTextLimit_ ||
            change.size() > amountTextLimit_) {
            return {tariffComparisonEnglish(Key::DisplayRange)};
        }
        const bool net=d.plan.exportPayment.has_value();
        const auto withoutKey=d.plan.restrictedCreditAtClose?Key::WithoutCreditCharge:net?Key::WithoutNetCharge:Key::WithoutCharge;
        const auto withKey=d.plan.restrictedCreditAtClose?Key::WithCreditCharge:net?Key::WithNetCharge:Key::WithCharge;
        return {tariffComparisonEnglish(withoutKey, {{"amount", baseCharge}, {"currency", d.plan.currency}}),
                tariffComparisonEnglish(withKey, {{"amount", withCharge}, {"currency", d.plan.currency}}),
                tariffComparisonEnglish(Key::Difference, {{"amount", change}, {"currency", d.plan.currency}})};
    }
};

}  // namespace om
