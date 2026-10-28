// Bounded base-10 arithmetic. No binary floating-point storage, inferred token scale or implicit rounding.
#pragma once
#include <cstdint>
#include <string>

namespace om {
class ExactDecimal {
   public:
    static constexpr unsigned maxDigits = 96, maxScale = 48;
    static bool parse(const std::string &text, ExactDecimal *out);
    std::string text() const;
    bool zero() const { return coefficient_ == "0"; }
    bool negative() const { return negative_; }
    unsigned scale() const { return scale_; }
    size_t digits() const { return coefficient_.size(); }
    int compare(const ExactDecimal &other) const;
    bool add(const ExactDecimal &other, ExactDecimal *out) const;
    bool subtract(const ExactDecimal &other, ExactDecimal *out) const;
    bool multiply(const ExactDecimal &other, ExactDecimal *out) const;
    // Explicit display-only round-half-even, including nonterminating means; underlying sum stays exact.
    bool formatRatio(uint32_t denominator, unsigned places, std::string *out) const;
   private:
    std::string coefficient_ = "0";
    unsigned scale_ = 0;
    bool negative_ = false;
    static bool make(std::string coefficient, unsigned scale, bool negative, ExactDecimal *out);
};
}  // namespace om
