#include "paths.hpp"

#include <cstdlib>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace cougar::paths {

namespace {

std::filesystem::path env_dir(const char *var, const std::filesystem::path &fallback)
{
    const char *value = std::getenv(var);
    return value && *value ? std::filesystem::path(value) : fallback;
}

std::filesystem::path home()
{
    return env_dir("HOME", "/tmp");
}

}  // namespace

std::filesystem::path config_file()
{
    return env_dir("XDG_CONFIG_HOME", home() / ".config") / "cougar-rgb" / "config.json";
}

std::filesystem::path lock_file()
{
    return env_dir("XDG_RUNTIME_DIR", "/tmp") / "cougar-rgb.lock";
}

std::filesystem::path user_service()
{
    return env_dir("XDG_CONFIG_HOME", home() / ".config") / "systemd" / "user" / "cougar-rgb.service";
}

std::filesystem::path system_service()
{
    return "/usr/lib/systemd/user/cougar-rgb.service";
}

std::filesystem::path desktop_entry()
{
    return env_dir("XDG_DATA_HOME", home() / ".local" / "share") / "applications" / "cougar-rgb.desktop";
}

std::filesystem::path self_executable()
{
    std::error_code ec;
    return std::filesystem::read_symlink("/proc/self/exe", ec);
}

std::filesystem::path sibling_executable(const char *name)
{
    const auto candidate = self_executable().parent_path() / name;
    std::error_code ec;
    return std::filesystem::exists(candidate, ec) ? candidate : std::filesystem::path(name);
}

bool daemon_running()
{
    const int fd = ::open(lock_file().c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    const bool locked = ::flock(fd, LOCK_EX | LOCK_NB) != 0;
    if (!locked)
        ::flock(fd, LOCK_UN);
    ::close(fd);
    return locked;
}

}  // namespace cougar::paths
