// Standard-input synthetic arithmetic oracle seam; no files/network or production feature toggle.
#include "product/exact_decimal.h"
#include <iostream>
int main() {
    std::string left, right;
    unsigned denominator = 0, places = 0;
    while (std::cin >> left >> right >> denominator >> places) {
        om::ExactDecimal a, b, sum, difference, product;
        std::string rounded;
        if (!om::ExactDecimal::parse(left, &a) || !om::ExactDecimal::parse(right, &b) || !a.add(b, &sum) || !a.subtract(b, &difference) ||
            !a.multiply(b, &product) || !a.formatRatio(denominator, places, &rounded)) return 1;
        std::cout << sum.text() << ' ' << difference.text() << ' ' << product.text() << ' ' << a.compare(b) << ' ' << rounded << '\n';
    }
    return 0;
}
