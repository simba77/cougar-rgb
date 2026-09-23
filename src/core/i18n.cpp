#include "i18n.hpp"

#include <cstdlib>
#include <unordered_map>

namespace cougar {

namespace {

constexpr std::string_view USAGE_EN = R"(Usage: cougar-rgb <command> [options]

Commands:
  list                      list effects
  info                      controller information
  set [EFFECT] [options]    choose the effect and its settings
  daemon                    background daemon
  gui                       graphical interface
  install                   install the systemd service and the menu entry
  uninstall                 remove the service and the menu entry

Options for set:
  -c, --color RRGGBB        color (can be repeated)
  -b, --brightness 0-100    brightness
  -s, --speed 1-10          speed
  --reverse, --no-reverse   reverse direction
  --random, --no-random     random colors
  --idle EFFECT             what to show during silence (music effects)
  --delay 0-1000            light delay in ms for the current output device
  --language LANG           interface language: system, en or ru

  --version                 version
  -h, --help                this help
)";

constexpr std::string_view USAGE_RU = R"(Использование: cougar-rgb <команда> [параметры]

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
  --language ЯЗЫК           язык интерфейса: system, en или ru

  --version                 версия
  -h, --help                эта справка
)";

constexpr Translation CATALOG[] = {
    // effects
    {"Static color", "Статичный цвет"},
    {"Breathing", "Дыхание"},
    {"Flash", "Вспышки"},
    {"Double flash", "Двойные вспышки"},
    {"Color cycle", "Смена цветов"},
    {"Off", "Выключено"},
    {"Rotating rainbow", "Вращающаяся радуга"},
    {"Smooth spectrum", "Плавный спектр"},
    {"Smooth breathing", "Плавное дыхание"},
    {"Comet", "Комета"},
    {"Rotating gradient", "Вращающийся градиент"},
    {"Fire", "Пламя"},
    {"Random colors", "Случайные цвета"},
    {"Bass pulse", "Пульс по басу"},
    {"Spectrum color", "Цвет по спектру"},
    {"Custom color per LED", "Свои цвета по диодам"},
    {"Speed", "Скорость"},
    {"Decay", "Затухание"},
    {"Response", "Реакция"},

    // GUI
    {"Color", "Цвет"},
    {"Colors", "Цвета"},
    {"Hardware", "Аппаратные"},
    {"Software (needs the daemon)", "Программные (нужна служба)"},
    {"Reverse direction", "Обратное направление"},
    {"When silent", "Когда тихо"},
    {"Light delay", "Задержка света"},
    {"Brightness", "Яркость"},
    {"Settings", "Параметры"},
    {"Language", "Язык"},
    {"System default", "Как в системе"},
    {"Start the daemon", "Запустить службу"},
    {"not found", "не найдено"},
    {"Output device: %1. Needed for Bluetooth headphones: the light waits until the sound reaches your ears.",
     "Для устройства вывода: %1. Нужна для Bluetooth-наушников — свет ждёт, пока звук дойдёт до ушей."},
    {"%1 ms", "%1 мс"},
    {"Controller error: %1", "Ошибка контроллера: %1"},
    {"The daemon is running", "Служба работает"},
    {"The daemon is not running: the software effect does not work",
     "Служба не запущена — программный эффект не работает"},
    {"The daemon is not running (the hardware effect was applied directly)",
     "Служба не запущена (аппаратный эффект применён напрямую)"},
    {"Case lighting", "Подсветка корпуса"},

    // CLI
    {USAGE_EN, USAGE_RU},
    {"Hardware (work without the daemon):", "Аппаратные (работают без службы):"},
    {"Software (need the daemon):", "Программные (нужна служба):"},
    {"%1: expected a number %2-%3", "%1: ожидается число %2-%3"},
    {"%1: expected one of: %2", "%1: ожидается одно из значений: %2"},
    {"%1: value required", "%1: нужно значение"},
    {"invalid color: %1", "неверный цвет: %1"},
    {"--idle: unknown effect %1", "--idle: неизвестный эффект %1"},
    {"unknown effect: %1 (see cougar-rgb list)", "неизвестный эффект: %1 (см. cougar-rgb list)"},
    {"unknown option: %1", "неизвестный параметр: %1"},
    {"unknown command: %1", "неизвестная команда: %1"},
    {"--idle only applies to music effects", "--idle задаётся только для музыкальных эффектов"},
    {"cannot determine the default output device", "не удалось определить устройство вывода по умолчанию"},
    {"Light delay %1 ms for \"%2\"", "Задержка света %1 мс для «%2»"},
    {"The software effect is saved, but the daemon is not running: cougar-rgb daemon "
     "or systemctl --user start cougar-rgb",
     "Программный эффект сохранён, но служба не запущена: cougar-rgb daemon "
     "или systemctl --user start cougar-rgb"},
    {"cannot start %1: %2", "не удалось запустить %1: %2"},
    {"No access to /dev/hidraw*: install the udev rule from packaging/60-rgb-fusion2.rules",
     "Нет доступа к /dev/hidraw*: нужно udev-правило из packaging/60-rgb-fusion2.rules"},
    {"Installed:", "Установлено:"},
};

Language g_language = Language::English;

const std::unordered_map<std::string_view, std::string_view> &russian()
{
    static const auto map = [] {
        std::unordered_map<std::string_view, std::string_view> m;
        for (const auto &t : CATALOG)
            m.emplace(t.english, t.russian);
        return m;
    }();
    return map;
}

}  // namespace

bool valid_language_setting(std::string_view setting)
{
    return setting == LANGUAGE_SYSTEM || parse_language(setting).has_value();
}

std::optional<Language> parse_language(std::string_view code)
{
    if (code == "en")
        return Language::English;
    if (code == "ru")
        return Language::Russian;
    return std::nullopt;
}

Language system_language()
{
    for (const char *var : {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char *value = std::getenv(var);
        if (!value || !*value)
            continue;
        const std::string_view locale(value);
        if (locale == "C" || locale == "POSIX")
            return Language::English;
        return locale.starts_with("ru") ? Language::Russian : Language::English;
    }
    return Language::English;
}

Language resolve_language(std::string_view setting)
{
    return parse_language(setting).value_or(system_language());
}

void set_language(Language language)
{
    g_language = language;
}

Language current_language()
{
    return g_language;
}

std::string_view tr(std::string_view english)
{
    if (g_language == Language::Russian)
        if (const auto it = russian().find(english); it != russian().end())
            return it->second;
    return english;
}

std::string_view cli_usage()
{
    return tr(USAGE_EN);
}

std::string trf(std::string_view english, std::initializer_list<std::string_view> args)
{
    const std::string_view text = tr(english);
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        const char next = i + 1 < text.size() ? text[i + 1] : '\0';
        if (text[i] == '%' && next >= '1' && next <= '9' && size_t(next - '1') < args.size()) {
            out += *(args.begin() + (next - '1'));
            ++i;
        } else {
            out += text[i];
        }
    }
    return out;
}

std::span<const Translation> catalog()
{
    return CATALOG;
}

}  // namespace cougar
