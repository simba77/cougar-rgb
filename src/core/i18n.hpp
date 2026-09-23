#pragma once

// Built-in translations shared by the CLI and the GUI. Source strings are English; the catalog
// maps them to Russian. Placeholders are %1..%9, like QString::arg, so GUI templates can use it.

#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace cougar {

enum class Language { English, Russian };

inline constexpr std::string_view LANGUAGE_SYSTEM = "system";

// "system", "en" or "ru"; anything else is invalid.
bool valid_language_setting(std::string_view setting);
std::optional<Language> parse_language(std::string_view code);

// Language from LANGUAGE, LC_ALL, LC_MESSAGES and LANG, in that order; English unless Russian.
Language system_language();
Language resolve_language(std::string_view setting);

void set_language(Language language);
Language current_language();

std::string_view tr(std::string_view english);
std::string_view cli_usage();
std::string trf(std::string_view english, std::initializer_list<std::string_view> args);

// Every catalog entry, for tests.
struct Translation {
    std::string_view english;
    std::string_view russian;
};
std::span<const Translation> catalog();

}  // namespace cougar
