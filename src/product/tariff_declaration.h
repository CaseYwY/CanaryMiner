// Reviewed offline composition and caller-declared opening; never a measured/reset ledger or mining permission.
#pragma once
#include "electricity.h"
#include "json_dom.h"
namespace om {
constexpr size_t tariffDeclarationMaxBytes=65536;
struct TariffDeclaration {
    std::string country, sourceReference, openingKey;
    uint64_t confirmedUtc=0;
    TariffPlan plan;
    BillOpening opening;
};
struct TariffProblem { std::string path, message; };
// Templates intentionally leave unknown rates, quantities and identities EMPTY, not zero.
JsonValue tariffDeclarationTemplate();
JsonValue tariffRowTemplate(const std::string &collection);
std::string tariffJson(const JsonValue &value);
// Transactional output; validates every key/type/rule and binds an explicit fixed clock.
bool decodeTariffDeclaration(const JsonValue &root,TariffDeclaration *out,TariffProblem *problem=nullptr);
bool parseTariffDeclaration(const std::string &text,JsonValue *root,TariffProblem *problem=nullptr);
// Path/value lines for explicit local review. Never log these: they contain private declarations.
std::vector<std::string> tariffDeclarationChanges(const JsonValue &before,const JsonValue &after);
} // namespace om
