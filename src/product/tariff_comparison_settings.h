// Read-only scenario calculation from a saved tariff and user-entered power.
#pragma once

#include <array>
#include <memory>
#include <thread>
#include "dashboard.h"
#include "tariff_declaration.h"
#include "tariff_comparison_messages.h"

namespace om {

class TariffComparisonSettings {
   public:
    bool visible = false;

    void open(const JsonValue &saved) {
        close();
        visible = true;
        TariffDeclaration checked;
        if (decodeTariffDeclaration(saved, &checked)) declaration_ = std::move(checked);
    }

    void key(int key, uint64_t nowUtc) {
        if (key == 27) { close(); return; }
        if (!declaration_ || pending()) return;
        if (key == '\t' || key == kKeyUp || key == kKeyDown || key == kKeyBackTab) {
            focus_ = 1 - focus_;
        } else if (key == '\r' || key == '\n') {
            calculate(nowUtc);
        } else if (key == 8 || key == 127) {
            auto &value = input_[focus_];
            if (!value.empty()) value.pop_back();
            clearResult();
        } else if (key >= 32 && key <= 255) {
            auto &value = input_[focus_];
            // Keep an invalid byte or overflow marker so a malformed answer cannot become a valid prefix.
            if (value.size() <= inputLimit_) value.push_back(key <= 126 ? static_cast<char>(key) : '?');
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
        add(tariffComparisonEnglish(Key::Span, {{"begin", std::to_string(o.throughUtcMs)}, {"end", std::to_string(o.cycleEndUtcMs)}}));
        add(tariffComparisonEnglish(Key::Instruction));
        add(std::string(focus_ == 0 ? "> " : "  ") + tariffComparisonEnglish(Key::WithoutWatts, {{"watts", input_[0]}}));
        add(std::string(focus_ == 1 ? "> " : "  ") + tariffComparisonEnglish(Key::WithWatts, {{"watts", input_[1]}}));
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
    size_t focus_ = 0;
    std::array<std::string, 2> input_;
    std::string notice_;
    std::shared_ptr<Job> job_;

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
        std::array<ExactDecimal, 2> watts;
        for (size_t i = 0; i < watts.size(); ++i) {
            const auto &value = input_[i];
            if (value.empty() || value.size() > inputLimit_ || value.find_first_not_of("0123456789.") != std::string::npos ||
                !ExactDecimal::parse(value, &watts[i])) {
                notice_ = tariffComparisonEnglish(Key::NeedWatts);
                return;
            }
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
            job->worker = std::thread([job, d, watts, decision = static_cast<int64_t>(nowUtc * 1000)] {
                try {
                    auto result = evaluate(d, watts, decision, job->cancel);
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
                                             int64_t decision, const std::atomic<bool> &cancel) {
        using i18n::tariffComparisonEnglish;
        const auto &o = d.opening;
        const PowerTrace base{o.evidence, {{o.throughUtcMs, o.cycleEndUtcMs, watts[0]}}};
        const PowerTrace with{o.evidence, {{o.throughUtcMs, o.cycleEndUtcMs, watts[1]}}};
        const auto result = counterfactualBill(d.plan, o, base, with, o.cycleEndUtcMs, decision, &cancel);
        if (result.status != ElectricityStatus::Ok) {
            return {tariffComparisonEnglish(Key::ModelStatus, {{"status", electricityStatusName(result.status)}})};
        }
        std::string baseCharge, withCharge, change;
        if (!result.withoutMining.charge->format(6, &baseCharge) ||
            !result.withMining.charge->format(6, &withCharge) ||
            !result.incrementalCharge->format(6, &change) ||
            baseCharge.size() > amountTextLimit_ || withCharge.size() > amountTextLimit_ ||
            change.size() > amountTextLimit_) {
            return {tariffComparisonEnglish(Key::DisplayRange)};
        }
        return {tariffComparisonEnglish(Key::WithoutCharge, {{"amount", baseCharge}, {"currency", d.plan.currency}}),
                tariffComparisonEnglish(Key::WithCharge, {{"amount", withCharge}, {"currency", d.plan.currency}}),
                tariffComparisonEnglish(Key::Difference, {{"amount", change}, {"currency", d.plan.currency}})};
    }
};

}  // namespace om
