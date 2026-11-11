// Pure canonical typed document admission, not a file read, saved receipt or runtime activation.
#pragma once
#include "preferences_protocol.h"
namespace om {
bool canonicalPreferencesDocument(PreferencesKind,const std::string &,bool allowEmptyTariffDraft=false);
} // namespace om
