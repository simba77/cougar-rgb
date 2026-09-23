#include "config.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

using namespace cougar;

TEST_CASE("defaults cover every effect")
{
    const Config cfg;
    for (const auto &e : effects()) {
        const EffectOptions &opts = cfg.effects.at(e->name());
        CHECK(opts.colors.size() >= size_t(e->info().colors));
        CHECK(opts.idle.has_value() == e->info().audio);
    }
}

TEST_CASE("save and load roundtrip")
{
    const auto path = std::filesystem::temp_directory_path() / "cougar-rgb-test" / "cfg" / "config.json";
    std::filesystem::remove_all(path.parent_path());
    Config cfg;
    cfg.effect = "sw_comet";
    cfg.brightness = 42;
    cfg.audio_delays = {{"bt_sink", 250}};
    cfg.options("sw_comet").colors = {"#010203", "#040506"};
    cfg.save(path);

    const Config loaded = Config::load(path);
    CHECK(loaded.effect == "sw_comet");
    CHECK(loaded.brightness == 42);
    CHECK(loaded.audio_delays == std::map<std::string, int>{{"bt_sink", 250}});
    CHECK(loaded.effects.at("sw_comet").colors == std::vector<std::string>{"#010203", "#040506"});
    std::filesystem::remove_all(path.parent_path().parent_path());
}

TEST_CASE("reads the Python version's config format")
{
    const Config cfg = Config::parse(R"({"effect": "sw_spectrum_color", "brightness": 80,
        "audio_delays": {"alsa_output.pci-0000_00_1f.3.analog-stereo": 0},
        "effects": {"sw_spectrum_color": {"colors": [], "speed": 10, "reverse": false, "random": false,
                                          "idle": "sw_fire"}}})");
    CHECK(cfg.effect == "sw_spectrum_color");
    CHECK(cfg.brightness == 80);
    CHECK(cfg.effects.at("sw_spectrum_color").speed == 10);
    CHECK(cfg.effects.at("sw_spectrum_color").idle == "sw_fire");
}

TEST_CASE("invalid values fall back to defaults")
{
    const Config cfg = Config::parse(R"({"effect": "removed_effect", "brightness": "loud",
        "effects": {"sw_bass_pulse": {"idle": "removed_effect", "bogus": 1, "speed": "fast"},
                    "sw_comet": {"colors": ["#ffffff"]}}})");
    CHECK(find_effect(cfg.effect));
    CHECK(cfg.brightness == 100);
    CHECK(cfg.effects.at("sw_bass_pulse").idle == std::string(IDLE_DEFAULT));
    CHECK(cfg.effects.at("sw_bass_pulse").speed == 5);
    CHECK(cfg.effects.at("sw_comet").colors.size() == 2);    // недостающий цвет дополнен
}

TEST_CASE("broken file gives defaults")
{
    CHECK(find_effect(Config::parse("{not json").effect));
    CHECK(find_effect(Config::load("/nonexistent/config.json").effect));
}

TEST_CASE("audio params carry the idle effect's own settings")
{
    Config cfg;
    cfg.options("sw_bass_pulse").idle = "sw_fire";
    cfg.options("sw_fire").speed = 9;
    const Params p = cfg.params("sw_bass_pulse");
    REQUIRE(p.idle);
    CHECK((p.idle->name == "sw_fire" && p.idle->speed == 9));

    cfg.options("sw_bass_pulse").idle = std::string(IDLE_NONE);
    CHECK_FALSE(cfg.params("sw_bass_pulse").idle);
}
