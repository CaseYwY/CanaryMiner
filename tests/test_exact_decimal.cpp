#include "product/exact_decimal.h"
#include "product_test_util.h"

using namespace om;
ExactDecimal number(const std::string &s) { ExactDecimal n; CHECK(ExactDecimal::parse(s, &n)); return n; }
int main() {
    ExactDecimal n = number("12.34"), out;
    for (const auto &s : {"", "-", "+1", "01", "00.1", "1.", ".1", " 1", "1 ", "1e3", "NaN", "inf", "1,2", "--1"}) {
        CHECK(!ExactDecimal::parse(s, &n)); CHECK(n.text() == "12.34");
    }
    CHECK(!ExactDecimal::parse("1", nullptr));
    CHECK(number("-0.000").text() == "0"); CHECK(number("0.001200").text() == "0.0012");
    CHECK(number("100.00").text() == "100");
    CHECK(number("0.1").add(number("0.2"), &out)); CHECK(out.text() == "0.3");
    CHECK(number("-1.01").add(number("2"), &out)); CHECK(out.text() == "0.99");
    CHECK(number("1").subtract(number("1.01"), &out)); CHECK(out.text() == "-0.01");
    CHECK(number("-1").subtract(number("-1.01"), &out)); CHECK(out.text() == "0.01");
    CHECK(number("2.50").multiply(number("0.98"), &out)); CHECK(out.text() == "2.45");
    CHECK(number("-2.50").multiply(number("0.98"), &out)); CHECK(out.text() == "-2.45");
    CHECK(number("0").multiply(number("-999"), &out)); CHECK(out.text() == "0");
    CHECK(number("0").compare(number("0.001")) < 0); CHECK(number("-1.001").compare(number("-1")) < 0);
    CHECK(number("1.100").compare(number("1.1")) == 0);
    CHECK(ExactDecimal::parse(std::string(96, '9'), &n));
    out = number("123"); CHECK(!n.add(number("1"), &out)); CHECK(out.text() == "123");
    CHECK(!n.multiply(number("10"), &out)); CHECK(out.text() == "123");
    CHECK(!ExactDecimal::parse(std::string(97, '9'), &n));
    CHECK(!ExactDecimal::parse("0." + std::string(48, '0') + "1", &n));
    n = number("0." + std::string(47, '0') + "1");
    CHECK(!n.multiply(number("0.1"), &out)); CHECK(out.text() == "123");
    std::string text;
    for (auto entry : {std::pair<const char *, const char *>{"1.245", "1.24"}, {"1.255", "1.26"}, {"-1.245", "-1.24"},
                       {"-1.255", "-1.26"}, {"0.005", "0.00"}, {"0.015", "0.02"}, {"9.999", "10.00"}, {"-0.0001", "0.00"}}) {
        CHECK(number(entry.first).formatRatio(1, 2, &text)); CHECK(text == entry.second);
    }
    CHECK(number("1").formatRatio(3, 6, &text)); CHECK(text == "0.333333");
    CHECK(number("2").formatRatio(3, 6, &text)); CHECK(text == "0.666667");
    CHECK(number("0.015").formatRatio(3, 2, &text)); CHECK(text == "0.00");
    CHECK(number("0.0150001").formatRatio(3, 2, &text)); CHECK(text == "0.01");
    CHECK(number("1").formatRatio(UINT32_MAX, 24, &text)); CHECK(text == "0.000000000232830643708080");
    CHECK(!number("1").formatRatio(0, 2, &text)); CHECK(!number("1").formatRatio(1, 25, &text));
    for (int a = -20; a <= 20; ++a) for (int b = -20; b <= 20; ++b) {
        CHECK(number(std::to_string(a)).add(number(std::to_string(b)), &out)); CHECK(out.text() == std::to_string(a + b));
        CHECK(number(std::to_string(a)).subtract(number(std::to_string(b)), &out)); CHECK(out.text() == std::to_string(a - b));
        CHECK(number(std::to_string(a)).multiply(number(std::to_string(b)), &out)); CHECK(out.text() == std::to_string(a * b));
    }
    return finishTests("exact_decimal");
}
