#include "effects.hpp"
#include "i18n.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace cougar;

namespace {

// Sets locale variables for one test and restores them afterwards.
struct Env {
    Env(std::initializer_list<std::pair<const char *, const char *>> values)
    {
        for (const char *var : {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"}) {
            const char *old = std::getenv(var);
            saved_.push_back({var, old ? std::optional<std::string>(old) : std::nullopt});
            ::unsetenv(var);
        }
        for (const auto &[var, value] : values)
            ::setenv(var, value, 1);
    }
    ~Env()
    {
        for (const auto &[var, value] : saved_)
            value ? ::setenv(var, value->c_str(), 1) : ::unsetenv(var);
        set_language(Language::English);
    }
    std::vector<std::pair<const char *, std::optional<std::string>>> saved_;
};

}  // namespace

TEST_CASE("system language comes from the locale variables")
{
    {
        Env env{{"LANG", "ru_RU.UTF-8"}};
        CHECK(system_language() == Language::Russian);
    }
    {
        Env env{{"LANG", "de_DE.UTF-8"}};
        CHECK(system_language() == Language::English);
    }
    {
        Env env{{"LANG", "ru_RU.UTF-8"}, {"LC_MESSAGES", "en_US.UTF-8"}};
        CHECK(system_language() == Language::English);
    }
    {
        Env env{{"LANG", "en_US.UTF-8"}, {"LANGUAGE", "ru:en"}};
        CHECK(system_language() == Language::Russian);
    }
    {
        Env env{{"LC_ALL", "C"}, {"LANG", "ru_RU.UTF-8"}};
        CHECK(system_language() == Language::English);
    }
    {
        Env env{};
        CHECK(system_language() == Language::English);
    }
}

TEST_CASE("explicit language setting overrides the system one")
{
    Env env{{"LANG", "ru_RU.UTF-8"}};
    CHECK(resolve_language("en") == Language::English);
    CHECK(resolve_language("ru") == Language::Russian);
    CHECK(resolve_language("system") == Language::Russian);
    CHECK(valid_language_setting("system"));
    CHECK_FALSE(valid_language_setting("de"));
}

TEST_CASE("tr translates and falls back to English")
{
    Env env{};
    set_language(Language::English);
    CHECK(tr("Brightness") == "Brightness");
    set_language(Language::Russian);
    CHECK(tr("Brightness") == "Яркость");
    CHECK(tr("not in the catalog") == "not in the catalog");
}

TEST_CASE("trf substitutes numbered placeholders")
{
    Env env{};
    set_language(Language::Russian);
    CHECK(trf("%1: expected a number %2-%3", {"--speed", "1", "10"}) == "--speed: ожидается число 1-10");
    set_language(Language::English);
    CHECK(trf("%1 ms", {"250"}) == "250 ms");
}

TEST_CASE("every effect title has a Russian translation")
{
    Env env{};
    set_language(Language::Russian);
    for (const auto &e : effects()) {
        INFO(e->name());
        CHECK(tr(e->info().title) != e->info().title);
        CHECK(tr(e->info().speed_title) != e->info().speed_title);
    }
}

TEST_CASE("catalog has no duplicate keys and keeps placeholders")
{
    std::set<std::string_view> keys;
    for (const auto &t : catalog()) {
        INFO(t.english);
        CHECK(keys.insert(t.english).second);
        for (char n = '1'; n <= '9'; ++n) {
            const std::string placeholder{'%', n};
            CHECK((t.english.find(placeholder) == std::string_view::npos) ==
                  (t.russian.find(placeholder) == std::string_view::npos));
        }
    }
}
