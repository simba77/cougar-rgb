#include "config.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace cougar {

using nlohmann::json;

namespace {

EffectOptions defaults_for(const Effect &effect)
{
    EffectOptions opts;
    opts.colors = effect.info().default_colors;
    if (effect.info().audio)
        opts.idle = std::string(IDLE_DEFAULT);
    return opts;
}

bool valid_idle(const std::string &name)
{
    const auto &choices = idle_choices();
    return std::find(choices.begin(), choices.end(), name) != choices.end();
}

template <typename T>
void read(const json &obj, const char *key, T &out)
{
    const auto it = obj.find(key);
    if (it == obj.end())
        return;
    try {
        out = it->get<T>();
    } catch (const json::exception &) {
        // wrong type: keep the default
    }
}

}  // namespace

Config::Config() : effect(DEFAULT_EFFECT)
{
    for (const auto &e : cougar::effects())
        effects[e->name()] = defaults_for(*e);
}

Config Config::parse(const std::string &text)
{
    Config cfg;
    const json data = json::parse(text, nullptr, false);
    if (!data.is_object())
        return cfg;

    std::string effect;
    read(data, "effect", effect);
    if (find_effect(effect))
        cfg.effect = effect;
    read(data, "brightness", cfg.brightness);
    if (const auto it = data.find("audio_delays"); it != data.end() && it->is_object())
        for (const auto &[sink, value] : it->items())
            if (value.is_number())
                cfg.audio_delays[sink] = value.get<int>();

    const auto stored = data.find("effects");
    for (const auto &e : cougar::effects()) {
        EffectOptions &opts = cfg.effects[e->name()];
        if (stored == data.end() || !stored->is_object() || !stored->contains(e->name()))
            continue;
        const json &obj = (*stored)[e->name()];
        if (!obj.is_object())
            continue;
        read(obj, "colors", opts.colors);
        read(obj, "speed", opts.speed);
        read(obj, "reverse", opts.reverse);
        read(obj, "random", opts.random);
        if (opts.idle) {
            std::string idle;
            read(obj, "idle", idle);
            if (valid_idle(idle))
                opts.idle = idle;
        }
        const auto &defaults = e->info().default_colors;
        if (opts.colors.size() < size_t(e->info().colors))
            opts.colors.insert(opts.colors.end(), defaults.begin() + std::min(opts.colors.size(), defaults.size()),
                               defaults.end());
    }
    return cfg;
}

Config Config::load(const std::filesystem::path &path)
{
    std::ifstream in(path);
    if (!in)
        return Config();
    std::stringstream text;
    text << in.rdbuf();
    return parse(text.str());
}

std::string Config::dump() const
{
    json data;
    data["effect"] = effect;
    data["brightness"] = brightness;
    data["audio_delays"] = audio_delays;
    json list = json::object();
    for (const auto &[name, opts] : effects) {
        json obj{{"colors", opts.colors}, {"speed", opts.speed}, {"reverse", opts.reverse}, {"random", opts.random}};
        if (opts.idle)
            obj["idle"] = *opts.idle;
        list[name] = obj;
    }
    data["effects"] = list;
    return data.dump(2);
}

void Config::save(const std::filesystem::path &path) const
{
    std::filesystem::create_directories(path.parent_path());
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << dump() << '\n';
    }
    std::filesystem::rename(tmp, path);
}

EffectOptions &Config::options(const std::string &name)
{
    return effects.at(name.empty() ? effect : name);
}

Params Config::params(const std::string &name) const
{
    const std::string key = name.empty() ? effect : name;
    const EffectOptions &opts = effects.at(key);
    Params p;
    p.name = key;
    p.colors.clear();
    for (const auto &c : opts.colors)
        p.colors.push_back(parse_color(c).value_or(Rgb{255, 255, 255}));
    if (p.colors.empty())
        p.colors.push_back({255, 255, 255});
    p.brightness = brightness;
    p.speed = opts.speed;
    p.reverse = opts.reverse;
    p.random = opts.random;
    if (opts.idle && find_effect(*opts.idle))
        p.idle = std::make_shared<Params>(params(*opts.idle));
    return p;
}

}  // namespace cougar
