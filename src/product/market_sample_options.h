#pragma once

#include "public_https.h"

namespace om {

inline bool parsePublicMarketSampleCli(int argc, const char *const *argv, int begin,
                                       std::string_view consent, std::string &dnsServer) {
    if (!argv || begin < 1 || begin >= argc || std::string_view(argv[begin]) != consent)
        return false;
    const int count = argc - begin;
    if (count == 1) {
        dnsServer.clear();
    } else if (count == 3 && std::string_view(argv[begin + 1]) == "--dns-server" &&
               validPublicDnsServer(argv[begin + 2])) {
        dnsServer = argv[begin + 2];
    } else return false;
    return true;
}

}  // namespace om
