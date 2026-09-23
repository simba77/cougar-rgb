// Installs the systemd user service and the menu entry (for running from the tarball, without the package).

#include "cli.hpp"

#include "paths.hpp"

#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace cougar::cli {

namespace {

int run(std::vector<std::string> args, bool quiet = false)
{
    std::vector<char *> argv;
    for (auto &a : args)
        argv.push_back(a.data());
    argv.push_back(nullptr);
    const pid_t pid = ::fork();
    if (pid == 0) {
        if (quiet) {
            const int null = ::open("/dev/null", O_WRONLY);
            ::dup2(null, STDOUT_FILENO);
            ::dup2(null, STDERR_FILENO);
        }
        ::execvp(argv[0], argv.data());
        ::_exit(127);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

// Desktops cache menu entries: without a cache refresh the menu may keep launching the old command.
void refresh_menu_cache()
{
    const auto dir = paths::desktop_entry().parent_path().string();
    run({"update-desktop-database", dir}, true);
    if (run({"kbuildsycoca6"}, true) == 127)
        run({"kbuildsycoca5"}, true);
}

void write_file(const std::filesystem::path &path, const std::string &content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::trunc) << content;
}

}  // namespace

int install()
{
    if (std::filesystem::exists(paths::system_service())) {
        // The service and the menu entry come with the package; enabling is enough.
        return run({"systemctl", "--user", "enable", "--now", "cougar-rgb.service"});
    }
    const auto self = paths::self_executable().string();
    write_file(paths::user_service(), "[Unit]\n"
                                      "Description=Cougar case ARGB lighting (Gigabyte RGB Fusion 2)\n"
                                      "After=graphical-session.target\n\n"
                                      "[Service]\n"
                                      "ExecStart=" + self + " daemon\n"
                                      "Restart=on-failure\n"
                                      "RestartSec=3\n\n"
                                      "[Install]\n"
                                      "WantedBy=default.target\n");
    write_file(paths::desktop_entry(), "[Desktop Entry]\n"
                                       "Type=Application\n"
                                       "Name=Cougar RGB\n"
                                       "Comment=Подсветка корпуса\n"
                                       "Exec=" + paths::sibling_executable("cougar-rgb-gui").string() + "\n"
                                       "Icon=preferences-desktop-color\n"
                                       "Categories=Settings;HardwareSettings;\n");
    refresh_menu_cache();
    if (int rc = run({"systemctl", "--user", "daemon-reload"}); rc != 0)
        return rc;
    if (int rc = run({"systemctl", "--user", "enable", "--now", "cougar-rgb.service"}); rc != 0)
        return rc;
    std::printf("Установлено:\n  %s\n  %s\n", paths::user_service().c_str(), paths::desktop_entry().c_str());
    return 0;
}

int uninstall()
{
    run({"systemctl", "--user", "disable", "--now", "cougar-rgb.service"});
    std::error_code ec;
    std::filesystem::remove(paths::user_service(), ec);
    std::filesystem::remove(paths::desktop_entry(), ec);
    refresh_menu_cache();
    run({"systemctl", "--user", "daemon-reload"});
    return 0;
}

}  // namespace cougar::cli
