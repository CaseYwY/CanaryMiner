#include "exact_decimal.h"
#include <algorithm>
#include <vector>

namespace om {
namespace {
std::string strip(std::string value) {
    const auto first = value.find_first_not_of('0');
    return first == std::string::npos ? "0" : value.substr(first);
}
int compareDigits(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    return a == b ? 0 : a < b ? -1 : 1;
}
std::string plus(const std::string &a, const std::string &b) {
    std::string result;
    unsigned carry = 0;
    for (size_t i = 0; i < (std::max)(a.size(), b.size()) || carry; ++i) {
        unsigned n = carry;
        if (i < a.size()) n += static_cast<unsigned>(a[a.size() - i - 1] - '0');
        if (i < b.size()) n += static_cast<unsigned>(b[b.size() - i - 1] - '0');
        result.push_back(static_cast<char>('0' + n % 10)); carry = n / 10;
    }
    std::reverse(result.begin(), result.end()); return result;
}
std::string minus(const std::string &a, const std::string &b) {  // a >= b
    std::string result;
    int borrow = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        int n = a[a.size() - i - 1] - '0' - borrow;
        if (i < b.size()) n -= b[b.size() - i - 1] - '0';
        borrow = n < 0 ? 1 : 0;
        result.push_back(static_cast<char>('0' + n + borrow * 10));
    }
    std::reverse(result.begin(), result.end()); return strip(result);
}
}  // namespace

bool ExactDecimal::make(std::string coefficient, unsigned scale, bool negative, ExactDecimal *out) {
    if (!out) return false;
    coefficient = strip(std::move(coefficient));
    while (scale && coefficient.size() > 1 && coefficient.back() == '0') { coefficient.pop_back(); --scale; }
    if (coefficient == "0") { scale = 0; negative = false; }
    if (coefficient.size() > maxDigits || scale > maxScale) return false;
    ExactDecimal result;
    result.coefficient_ = std::move(coefficient); result.scale_ = scale; result.negative_ = negative;
    *out = std::move(result); return true;
}
bool ExactDecimal::parse(const std::string &s, ExactDecimal *out) {
    if (!out || s.empty() || s.size() > maxDigits + maxScale + 2) return false;
    size_t i = s[0] == '-' ? 1 : 0;
    const bool negative = i != 0;
    if (i == s.size() || s[i] < '0' || s[i] > '9') return false;
    const size_t integerStart = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
    if (i - integerStart > 1 && s[integerStart] == '0') return false;
    std::string coefficient = s.substr(integerStart, i - integerStart);
    unsigned scale = 0;
    if (i < s.size() && s[i] == '.') {
        const size_t start = ++i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        if (i == start || i - start > maxScale) return false;
        scale = static_cast<unsigned>(i - start); coefficient += s.substr(start, scale);
    }
    if (i != s.size()) return false;
    return make(std::move(coefficient), scale, negative, out);
}
std::string ExactDecimal::text() const {
    std::string value = coefficient_;
    if (scale_) {
        if (value.size() <= scale_) value.insert(0, scale_ + 1 - value.size(), '0');
        value.insert(value.size() - scale_, 1, '.');
    }
    return (negative_ ? "-" : "") + value;
}
int ExactDecimal::compare(const ExactDecimal &b) const {
    if (negative_ != b.negative_) return negative_ ? -1 : 1;
    const auto scale = (std::max)(scale_, b.scale_);
    const int order = compareDigits(strip(coefficient_ + std::string(scale - scale_, '0')), strip(b.coefficient_ + std::string(scale - b.scale_, '0')));
    return negative_ ? -order : order;
}
bool ExactDecimal::add(const ExactDecimal &b, ExactDecimal *out) const {
    const auto scale = (std::max)(scale_, b.scale_);
    const auto left = strip(coefficient_ + std::string(scale - scale_, '0')), right = strip(b.coefficient_ + std::string(scale - b.scale_, '0'));
    if (negative_ == b.negative_) return make(plus(left, right), scale, negative_, out);
    const int order = compareDigits(left, right);
    return order >= 0 ? make(minus(left, right), scale, negative_, out) : make(minus(right, left), scale, b.negative_, out);
}
bool ExactDecimal::subtract(const ExactDecimal &b, ExactDecimal *out) const {
    auto negative = b;
    if (!negative.zero()) negative.negative_ = !negative.negative_;
    return add(negative, out);
}
bool ExactDecimal::multiply(const ExactDecimal &b, ExactDecimal *out) const {
    std::vector<unsigned> digits(coefficient_.size() + b.coefficient_.size(), 0);
    for (size_t i = coefficient_.size(); i-- > 0;) for (size_t j = b.coefficient_.size(); j-- > 0;)
        digits[i + j + 1] += static_cast<unsigned>((coefficient_[i] - '0') * (b.coefficient_[j] - '0'));
    for (size_t i = digits.size(); i-- > 1;) { digits[i - 1] += digits[i] / 10; digits[i] %= 10; }
    std::string result;
    for (unsigned digit : digits) result.push_back(static_cast<char>('0' + digit));
    return make(std::move(result), scale_ + b.scale_, negative_ != b.negative_, out);
}
bool ExactDecimal::formatRatio(uint32_t denominator, unsigned places, std::string *out) const {
    if (!out || !denominator || places > 24) return false;
    std::string digits = coefficient_;
    if (places > scale_) digits.append(places - scale_, '0');
    uint64_t remainder = 0;
    std::string quotient;
    for (char c : digits) {
        const uint64_t n = remainder * 10 + static_cast<unsigned>(c - '0');
        quotient.push_back(static_cast<char>('0' + n / denominator)); remainder = n % denominator;
    }
    int half = remainder * 2 < denominator ? -1 : remainder * 2 > denominator ? 1 : 0;
    if (scale_ > places) {
        const auto dropped = scale_ - places;
        if (quotient.size() <= dropped) quotient.insert(0, dropped + 1 - quotient.size(), '0');
        const auto first = quotient.size() - dropped;
        half = quotient[first] < '5' ? -1 : quotient[first] > '5' ? 1 : 0;
        if (!half && (remainder || quotient.find_first_not_of('0', first + 1) != std::string::npos)) half = 1;
        quotient.resize(first);
    }
    quotient = strip(quotient);
    if (half > 0 || (!half && (quotient.back() - '0') % 2)) quotient = plus(quotient, "1");
    const bool negative = negative_ && quotient != "0";
    if (places) {
        if (quotient.size() <= places) quotient.insert(0, places + 1 - quotient.size(), '0');
        quotient.insert(quotient.size() - places, 1, '.');
    }
    *out = (negative ? "-" : "") + quotient; return true;
}
bool ExactDecimal::formatRatio(const ExactDecimal &denominator,unsigned places,std::string *out)const{
    if(!out||denominator.zero()||denominator.negative()||places>24)return false;
    // At most 168 numerator / 144 denominator digits; never materialize an unbounded decimal expansion.
    const int shift=static_cast<int>(places)+static_cast<int>(denominator.scale_)-static_cast<int>(scale_);
    const auto dividend=coefficient_+std::string(shift>0?static_cast<size_t>(shift):0,'0');
    const auto divisor=denominator.coefficient_+std::string(shift<0?static_cast<size_t>(-shift):0,'0');
    std::string quotient,remainder="0";
    for(char c:dividend){
        remainder=strip(remainder+c);unsigned digit=0;
        while(compareDigits(remainder,divisor)>=0){remainder=minus(remainder,divisor);++digit;}
        quotient.push_back(static_cast<char>('0'+digit));
    }
    quotient=strip(quotient);const int half=compareDigits(plus(remainder,remainder),divisor);
    if(half>0||(!half&&(quotient.back()-'0')%2))quotient=plus(quotient,"1");
    const bool negative=negative_&&quotient!="0";
    if(places){if(quotient.size()<=places)quotient.insert(0,places+1-quotient.size(),'0');quotient.insert(quotient.size()-places,1,'.');}
    *out=(negative?"-":"")+quotient;return true;
}
}  // namespace om
