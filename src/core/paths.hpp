#pragma once

#include <filesystem>

namespace cougar::paths {

std::filesystem::path config_file();        // $XDG_CONFIG_HOME/cougar-rgb/config.json
std::filesystem::path lock_file();          // $XDG_RUNTIME_DIR/cougar-rgb.lock
std::filesystem::path user_service();       // ~/.config/systemd/user/cougar-rgb.service
std::filesystem::path system_service();     // from the package: /usr/lib/systemd/user/cougar-rgb.service
std::filesystem::path desktop_entry();      // ~/.local/share/applications/cougar-rgb.desktop
std::filesystem::path self_executable();
std::filesystem::path sibling_executable(const char *name);  // next to the current binary, or just the name

// Whether the daemon is running (by its lock file).
bool daemon_running();

}  // namespace cougar::paths
