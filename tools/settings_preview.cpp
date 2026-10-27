// Development-only native settings executable: no CUDA, pool connection or mining entry point.
#include <csignal>
#include <cstdio>
#include <string>

#include "../src/product/settings.h"

static volatile sig_atomic_t stopped = 0;
static void stop(int) { stopped = 1; }

int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--snapshot") {
        om::SettingsPanel panel;
        panel.open(om::UiPreferences{});
        printf("%s", panel.render(100, 24, om::ColorLevel::None).c_str());
        return 0;
    }
    if (argc != 1 && !(argc == 3 && std::string(argv[1]) == "--state-dir")) {
        fprintf(stderr, "usage: canary_settings_preview [--snapshot | --state-dir DIRECTORY]\nNo mining is included in this preview.\n");
        return 2;
    }
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    return om::runSettingsCommand(argc == 3 ? argv[2] : "state", &stopped);
}
