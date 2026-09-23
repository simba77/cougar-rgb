#pragma once

#include <filesystem>

namespace cougar::paths {

std::filesystem::path config_file();        // $XDG_CONFIG_HOME/cougar-rgb/config.json
std::filesystem::path lock_file();          // $XDG_RUNTIME_DIR/cougar-rgb.lock
std::filesystem::path user_service();       // ~/.config/systemd/user/cougar-rgb.service
std::filesystem::path system_service();     // из пакета: /usr/lib/systemd/user/cougar-rgb.service
std::filesystem::path desktop_entry();      // ~/.local/share/applications/cougar-rgb.desktop
std::filesystem::path self_executable();
std::filesystem::path sibling_executable(const char *name);  // рядом с текущим бинарником или просто имя

// Запущена ли служба (по блокировке файла).
bool daemon_running();

}  // namespace cougar::paths
