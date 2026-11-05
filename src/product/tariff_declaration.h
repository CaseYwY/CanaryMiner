// Reviewed offline composition and caller-declared opening; never a measured/reset ledger or mining permission.
#pragma once
#include "electricity.h"
#include "json_dom.h"
#include <cstddef>
namespace om {
constexpr size_t tariffDeclarationMaxBytes=65536;
struct TariffDeclaration {
    std::string country, sourceReference, openingKey;
    uint64_t confirmedUtc=0;
    TariffPlan plan;
    BillOpening opening;
};
struct TariffProblem { std::string path, message; };
// Display-neutral private source records. Row counts are not JSON strings; value tokens retain exact source serialization.
struct TariffDeclarationChange {
    enum class Kind { Value, RowCount };
    Kind kind=Kind::Value;
    std::string path,beforeJson,afterJson;
    std::size_t beforeRows=0,afterRows=0;
};
// Templates intentionally leave unknown rates, quantities and identities EMPTY, not zero.
JsonValue tariffDeclarationTemplate();
JsonValue tariffRowTemplate(const std::string &collection);
std::string tariffJson(const JsonValue &value);
// Transactional output; validates every key/type/rule and binds an explicit fixed clock.
bool decodeTariffDeclaration(const JsonValue &root,TariffDeclaration *out,TariffProblem *problem=nullptr);
bool parseTariffDeclaration(const std::string &text,JsonValue *root,TariffProblem *problem=nullptr);
// Path/value lines for explicit local review. Never log these: they contain private declarations.
std::vector<std::string> tariffDeclarationChanges(const JsonValue &before,const JsonValue &after);
// Same traversal/order and raw originals, without UI catalogs or preformatted human row-count descriptions. Never log.
std::vector<TariffDeclarationChange> tariffDeclarationChangeRecords(const JsonValue &before,const JsonValue &after);
} // namespace om
