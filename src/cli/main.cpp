// Command line: cougar-rgb <command>.

#include "cli.hpp"

#include "audio.hpp"
#include "config.hpp"
#include "device.hpp"
#include "effects.hpp"
#include "engine.hpp"
#include "i18n.hpp"
#include "paths.hpp"
#include "sink.hpp"
#include "version.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

using namespace cougar;

namespace {

[[noreturn]] void fail(const std::string &message)
{
    std::fprintf(stderr, "cougar-rgb: %s\n", message.c_str());
    std::exit(2);
}

int parse_int(std::string_view text, int lo, int hi, const char *what)
{
    int value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || ptr != text.data() + text.size() || value < lo || value > hi)
        fail(trf("%1: expected a number %2-%3", {what, std::to_string(lo), std::to_string(hi)}));
    return value;
}

int cmd_list()
{
    for (bool hw : {true, false}) {
        std::printf("%s\n", std::string(tr(hw ? "Hardware (work without the daemon):" : "Software (need the daemon):")).c_str());
        for (const auto &e : effects())
            if (e->hardware() == hw)
                std::printf("  %-18s %s\n", e->name().c_str(), std::string(tr(e->info().title)).c_str());
    }
    return 0;
}

int cmd_info()
{
    auto dev = Fusion2::open();
    const DeviceInfo info = dev->info();
    std::printf("%-10s %d\n%-10s %d\n%-10s %s\n%-10s %s\n%-10s %s\n%-10s %s\n", "product", info.product,
                "device_num", info.device_num, "firmware", info.firmware.c_str(), "name", info.name.c_str(),
                "chip_id", info.chip_id.c_str(), "hidraw", dev->path().c_str());
    std::printf("%-10s %s\n", "daemon", paths::daemon_running() ? "running" : "stopped");
    return 0;
}

int cmd_set(const std::vector<std::string_view> &args)
{
    Config cfg = Config::load(paths::config_file());
    std::optional<std::string> effect;
    std::vector<std::string> colors;
    std::optional<int> brightness, speed, delay;
    std::optional<bool> reverse, random;
    std::optional<std::string> idle, language;

    for (size_t i = 0; i < args.size(); ++i) {
        const auto arg = args[i];
        auto value = [&]() -> std::string_view {
            if (i + 1 >= args.size())
                fail(trf("%1: value required", {arg}));
            return args[++i];
        };
        if (arg == "-c" || arg == "--color") {
            const auto c = value();
            if (!parse_color(c))
                fail(trf("invalid color: %1", {c}));
            colors.push_back(to_hex(*parse_color(c)));
        } else if (arg == "-b" || arg == "--brightness") {
            brightness = parse_int(value(), 0, 100, "--brightness");
        } else if (arg == "-s" || arg == "--speed") {
            speed = parse_int(value(), 1, 10, "--speed");
        } else if (arg == "--delay") {
            delay = parse_int(value(), 0, int(MAX_DELAY * 1000), "--delay");
        } else if (arg == "--reverse" || arg == "--no-reverse") {
            reverse = arg == "--reverse";
        } else if (arg == "--random" || arg == "--no-random") {
            random = arg == "--random";
        } else if (arg == "--language") {
            language = std::string(value());
            if (!valid_language_setting(*language))
                fail(trf("%1: expected one of: %2", {"--language", "system, en, ru"}));
        } else if (arg == "--idle") {
            idle = std::string(value());
            const auto &choices = idle_choices();
            if (std::find(choices.begin(), choices.end(), *idle) == choices.end())
                fail(trf("--idle: unknown effect %1", {*idle}));
        } else if (!arg.starts_with('-') && !effect) {
            if (!find_effect(arg))
                fail(trf("unknown effect: %1 (see cougar-rgb list)", {arg}));
            effect = std::string(arg);
        } else {
            fail(trf("unknown option: %1", {arg}));
        }
    }

    if (effect)
        cfg.effect = *effect;
    EffectOptions &opts = cfg.options();
    if (!colors.empty())
        opts.colors = colors;
    if (speed)
        opts.speed = *speed;
    if (reverse)
        opts.reverse = *reverse;
    if (random)
        opts.random = *random;
    if (idle) {
        if (!find_effect(cfg.effect)->info().audio)
            fail(std::string(tr("--idle only applies to music effects")));
        opts.idle = *idle;
    }
    if (delay) {
        const auto sink = default_sink();
        if (!sink)
            fail(std::string(tr("cannot determine the default output device")));
        cfg.audio_delays[sink->name] = *delay;
        std::printf("%s\n", trf("Light delay %1 ms for \"%2\"", {std::to_string(*delay), sink->description}).c_str());
    }
    if (brightness)
        cfg.brightness = *brightness;
    if (language)
        cfg.language = *language;
    cfg.save(paths::config_file());

    if (paths::daemon_running())
        return 0;
    if (!find_effect(cfg.effect)->hardware()) {
        std::fprintf(stderr, "%s\n", std::string(tr("The software effect is saved, but the daemon is not running: "
                                                    "cougar-rgb daemon or systemctl --user start cougar-rgb")).c_str());
        return 0;
    }
    auto dev = Fusion2::open();
    Engine engine(*dev);
    engine.setup();
    engine.apply(cfg.effect, cfg.params());
    return 0;
}

int cmd_gui(char **argv)
{
    const auto gui = paths::sibling_executable("cougar-rgb-gui");
    argv[0] = const_cast<char *>(gui.c_str());
    ::execvp(gui.c_str(), argv);
    std::fprintf(stderr, "cougar-rgb: %s\n", trf("cannot start %1: %2", {gui.string(), std::strerror(errno)}).c_str());
    return 1;
}

}  // namespace

int main(int argc, char **argv)
{
    set_language(resolve_language(Config::load(paths::config_file()).language));
    std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.empty()) {
        std::fputs(std::string(cli_usage()).c_str(), stderr);
        return 2;
    }
    const auto command = args.front();
    const std::vector<std::string_view> rest(args.begin() + 1, args.end());
    try {
        if (command == "-h" || command == "--help")
            return std::fputs(std::string(cli_usage()).c_str(), stdout), 0;
        if (command == "--version")
            return std::printf("cougar-rgb %s\n", VERSION), 0;
        if (command == "list")
            return cmd_list();
        if (command == "info")
            return cmd_info();
        if (command == "set")
            return cmd_set(rest);
        if (command == "daemon")
            return cli::run_daemon();
        if (command == "gui")
            return cmd_gui(argv + 1);
        if (command == "install")
            return cli::install();
        if (command == "uninstall")
            return cli::uninstall();
        fail(trf("unknown command: %1", {command}));
    } catch (const DeviceAccessDenied &) {
        std::fprintf(stderr, "%s\n",
                     std::string(tr("No access to /dev/hidraw*: install the udev rule from "
                                    "packaging/60-rgb-fusion2.rules")).c_str());
    } catch (const DeviceError &e) {
        std::fprintf(stderr, "%s\n", e.what());
    }
    return 1;
}
