// Command line: cougar-rgb <command>.

#include "cli.hpp"

#include "audio.hpp"
#include "config.hpp"
#include "device.hpp"
#include "effects.hpp"
#include "engine.hpp"
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

constexpr const char *USAGE = R"(Использование: cougar-rgb <команда> [параметры]

Команды:
  list                      список эффектов
  info                      информация о контроллере
  set [ЭФФЕКТ] [параметры]  выбрать эффект и параметры
  daemon                    фоновая служба
  gui                       графический интерфейс
  install                   установить systemd-службу и ярлык
  uninstall                 удалить службу и ярлык

Параметры set:
  -c, --color RRGGBB        цвет (можно несколько раз)
  -b, --brightness 0-100    яркость
  -s, --speed 1-10          скорость
  --reverse, --no-reverse   обратное направление
  --random, --no-random     случайные цвета
  --idle ЭФФЕКТ             что показывать в тишине (для музыкальных эффектов)
  --delay 0-1000            задержка света в мс для текущего устройства вывода

  --version                 версия
  -h, --help                эта справка
)";

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
        fail(std::string(what) + ": ожидается число " + std::to_string(lo) + "-" + std::to_string(hi));
    return value;
}

int cmd_list()
{
    for (bool hw : {true, false}) {
        std::printf("%s\n", hw ? "Аппаратные (работают без службы):" : "Программные (нужна служба):");
        for (const auto &e : effects())
            if (e->hardware() == hw)
                std::printf("  %-18s %s\n", e->name().c_str(), e->info().title.c_str());
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
    std::optional<std::string> idle;

    for (size_t i = 0; i < args.size(); ++i) {
        const auto arg = args[i];
        auto value = [&]() -> std::string_view {
            if (i + 1 >= args.size())
                fail(std::string(arg) + ": нужно значение");
            return args[++i];
        };
        if (arg == "-c" || arg == "--color") {
            const auto c = value();
            if (!parse_color(c))
                fail("неверный цвет: " + std::string(c));
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
        } else if (arg == "--idle") {
            idle = std::string(value());
            const auto &choices = idle_choices();
            if (std::find(choices.begin(), choices.end(), *idle) == choices.end())
                fail("--idle: неизвестный эффект " + *idle);
        } else if (!arg.starts_with('-') && !effect) {
            if (!find_effect(arg))
                fail("неизвестный эффект: " + std::string(arg) + " (см. cougar-rgb list)");
            effect = std::string(arg);
        } else {
            fail("неизвестный параметр: " + std::string(arg));
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
            fail("--idle задаётся только для музыкальных эффектов");
        opts.idle = *idle;
    }
    if (delay) {
        const auto sink = default_sink();
        if (!sink)
            fail("не удалось определить устройство вывода по умолчанию");
        cfg.audio_delays[sink->name] = *delay;
        std::printf("Задержка света %d мс для «%s»\n", *delay, sink->description.c_str());
    }
    if (brightness)
        cfg.brightness = *brightness;
    cfg.save(paths::config_file());

    if (paths::daemon_running())
        return 0;
    if (!find_effect(cfg.effect)->hardware()) {
        std::fprintf(stderr, "Программный эффект сохранён, но служба не запущена: cougar-rgb daemon "
                             "или systemctl --user start cougar-rgb\n");
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
    std::fprintf(stderr, "cougar-rgb: не удалось запустить %s: %s\n", gui.c_str(), std::strerror(errno));
    return 1;
}

}  // namespace

int main(int argc, char **argv)
{
    std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.empty()) {
        std::fputs(USAGE, stderr);
        return 2;
    }
    const auto command = args.front();
    const std::vector<std::string_view> rest(args.begin() + 1, args.end());
    try {
        if (command == "-h" || command == "--help")
            return std::fputs(USAGE, stdout), 0;
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
        fail("неизвестная команда: " + std::string(command));
    } catch (const DeviceAccessDenied &) {
        std::fprintf(stderr, "Нет доступа к /dev/hidraw*: нужно udev-правило из packaging/60-rgb-fusion2.rules\n");
    } catch (const DeviceError &e) {
        std::fprintf(stderr, "%s\n", e.what());
    }
    return 1;
}
