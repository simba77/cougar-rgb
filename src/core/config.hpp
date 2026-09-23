#pragma once

// Settings in ~/.config/cougar-rgb/config.json. The daemon rereads the file when it changes.

#include "effects.hpp"
#include "i18n.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cougar {

inline constexpr std::string_view DEFAULT_EFFECT = "sw_rainbow";

struct EffectOptions {
    std::vector<std::string> colors;
    int speed = 5;
    bool reverse = false;
    bool random = false;
    std::optional<std::string> idle;        // audio effects only
};

class Config {
public:
    Config();                               // defaults

    static Config load(const std::filesystem::path &path);
    static Config parse(const std::string &json);
    void save(const std::filesystem::path &path) const;
    std::string dump() const;

    std::string effect;
    int brightness = 100;
    std::string language{LANGUAGE_SYSTEM};      // "system", "en" or "ru"
    std::map<std::string, int> audio_delays;
    std::map<std::string, EffectOptions> effects;

    EffectOptions &options(const std::string &name = {});
    Params params(const std::string &name = {}) const;
};

}  // namespace cougar
