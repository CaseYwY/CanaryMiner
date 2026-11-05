// UI-only source bridge. Arithmetic enums remain data; descriptions never supply supplier/source or permission facts.
#pragma once
#include "electricity.h"
#include "electricity_status_messages.h"
namespace om {
inline std::optional<i18n::ElectricityStatusMessageKey> electricityStatusSourceKey(ElectricityStatus status){
    using Status=ElectricityStatus;using Key=i18n::ElectricityStatusMessageKey;
    switch(status){
        case Status::Ok:return Key::Ok;case Status::Partial:return Key::Partial;
        case Status::Unavailable:return Key::Unavailable;case Status::Invalid:return Key::Invalid;
        case Status::Unsupported:return Key::Unsupported;case Status::MissingPrice:return Key::MissingPrice;
        case Status::Conflict:return Key::Conflict;case Status::NotYetKnown:return Key::NotYetKnown;
        case Status::ClockFault:return Key::ClockFault;case Status::Capacity:return Key::Capacity;
    }
    return std::nullopt;
}
inline std::string electricityStatusReasonEnglish(ElectricityStatus status,const std::string &original){
    const auto key=electricityStatusSourceKey(status);if(!key)return original;
    const auto source=i18n::electricityStatusEnglish(*key);return source==original?source:original;
}
} // namespace om
