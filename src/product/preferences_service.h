// Helper-side disk service. Call only in the owned helper, never from a renderer/control callback.
#pragma once
#include "preferences_protocol.h"
#include <memory>
namespace om {
class PreferencesService {
  public:
    PreferencesService();~PreferencesService();
    PreferencesService(const PreferencesService &)=delete;PreferencesService &operator=(const PreferencesService &)=delete;
    bool transact(const PreferencesRequest &,PreferencesReply *); // false = invalid protocol/sequence; no automatic replay
  private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace om
