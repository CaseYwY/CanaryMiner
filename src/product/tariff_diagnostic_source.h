// UI-only bridge from explicit data-owner classifications, never from parsed English or rendered values.
#pragma once
#include "tariff_declaration.h"
#include "tariff_diagnostic_messages.h"
namespace om {
inline std::optional<i18n::TariffDiagnosticMessageKey> tariffDiagnosticKey(TariffProblemKind kind){
    using Kind=TariffProblemKind;using Key=i18n::TariffDiagnosticMessageKey;
    switch(kind){
        case Kind::MissingFields:return Key::MissingFields;case Kind::SchemaFields:return Key::SchemaFields;
        case Kind::DuplicateField:return Key::DuplicateField;case Kind::Label:return Key::Label;
        case Kind::Integer:return Key::Integer;case Kind::Decimal:return Key::Decimal;
        case Kind::Choice:return Key::Choice;case Kind::Collection:return Key::Collection;
        case Kind::NoOutput:return Key::NoOutput;case Kind::Oversized:return Key::Oversized;
        case Kind::SchemaVersion:return Key::SchemaVersion;case Kind::Acknowledgement:return Key::Acknowledgement;
        case Kind::Confirmation:return Key::Confirmation;case Kind::Currency:return Key::Currency;
        case Kind::FixedOffset:return Key::FixedOffset;case Kind::Date:return Key::Date;
        case Kind::OpeningQuantity:return Key::OpeningQuantity;case Kind::ArithmeticCapacity:return Key::ArithmeticCapacity;
        case Kind::FuturePublication:return Key::FuturePublication;case Kind::InvalidJson:return Key::InvalidJson;
        case Kind::Unknown:case Kind::ModelStatus:return std::nullopt;
    }
    return std::nullopt;
}
inline std::string tariffDiagnosticReasonEnglish(const TariffProblem &problem){
    const auto key=tariffDiagnosticKey(problem.kind);if(!key)return problem.message;
    i18n::MessageArguments arguments;
    if(*key==i18n::TariffDiagnosticMessageKey::Choice){
        if(problem.choices.empty())return problem.message;
        std::string choices;for(const auto &choice:problem.choices){if(choice.empty())return problem.message;if(!choices.empty())choices+=" / ";choices+=choice;}
        arguments.emplace_back("choices",std::move(choices));
    }
    const auto source=i18n::tariffDiagnosticEnglish(*key,arguments);
    // A changed/unknown original is never replaced by a stale description. Equality is not parsing or source approval.
    return source==problem.message?source:problem.message;
}
} // namespace om
