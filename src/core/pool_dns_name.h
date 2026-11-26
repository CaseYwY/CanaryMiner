#pragma once
#include <string_view>

namespace om {
inline bool validPoolDnsName(std::string_view host) {
    if (host.empty() || host.size() > 253) return false;
    bool numericAddress = true;
    for (std::size_t begin = 0;;) {
        const auto dot = host.find('.', begin);
        const auto end = dot == host.npos ? host.size() : dot;
        const auto label = host.substr(begin, end - begin);
        if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-' ||
            label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") != label.npos)
            return false;

        // Native resolvers also recognize historic IPv4 spellings with abbreviated and base-prefixed fields.
        const bool decimalOrOctal = label.find_first_not_of("0123456789") == label.npos;
        const bool hexadecimal = label.size() > 2 && label.front() == '0' &&
            (label[1] == 'x' || label[1] == 'X') && label.find_first_not_of("0123456789abcdefABCDEF", 2) == label.npos;
        numericAddress = numericAddress && (decimalOrOctal || hexadecimal);
        if (end == host.size()) return !numericAddress;
        begin = end + 1;
    }
}
} // namespace om
