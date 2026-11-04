// Exact display-only numbers and maintained plural selection. Language and formatting locale are independent.
#pragma once
#include <string>
#include <vector>
namespace om {
enum class PresentationDigits { Latin, Locale };
struct PresentationNumberRequest {
    std::string decimal,formattingLocale="en",language="en";
    unsigned fractionDigits=2;
    bool grouping=true;
    PresentationDigits digits=PresentationDigits::Latin;
};
struct PresentationNumber {
    std::string text,ascii,formattingLocale,language,pluralCategory;
    bool backendFallback=false,localeFallback=false,languageFallback=false,resourceInheritance=false,pluralAvailable=false,rounded=false;
};
// Existing bounded ExactDecimal performs explicit round-half-even once. ICU only decorates that exact
// fixed decimal and selects cardinal plural using the visible fraction digits. Never a money conversion,
// asset-scale inference, parser, source mutation or machine-output formatter. Failure preserves output.
bool formatPresentationNumber(const PresentationNumberRequest &,PresentationNumber *);
const std::vector<std::string> &presentationFormattingLocales();
} // namespace om
