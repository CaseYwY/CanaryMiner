#pragma once

#include <signal.h>

#include <utility>

namespace om {

class PreparationCancellation final {
 public:
    void bind(volatile sig_atomic_t *stop) noexcept { stop_ = stop; }
    bool requested() const noexcept { return stop_ && *stop_ != 0; }

    template <typename Stage>
    bool runStage(Stage &&stage) {
        if (requested() || !std::forward<Stage>(stage)()) return false;
        return !requested();
    }

 private:
    volatile sig_atomic_t *stop_ = nullptr;
};

}  // namespace om
