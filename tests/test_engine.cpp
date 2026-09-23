#include "config.hpp"
#include "engine.hpp"
#include "fakes.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace cougar;
using test::FakeController;

TEST_CASE("setup blanks the other zones")
{
    test::TestClock clock;
    FakeController dev;
    Engine(dev).setup();
    int effects = 0;
    for (const auto &c : dev.calls)
        if (c.name == "set_effect") {
            ++effects;
            CHECK(c.zone != EFFECT_ZONE);
        }
    CHECK(effects == ZONE_COUNT - 1);
    CHECK(dev.names().back() == "apply");
}

TEST_CASE("hardware effect leaves direct mode")
{
    test::TestClock clock;
    FakeController dev;
    const Config cfg;
    Engine(dev).apply("static", cfg.params("static"));
    CHECK(dev.names() == std::vector<std::string>{"set_direct", "set_effect", "apply"});
    CHECK_FALSE(dev.calls[0].flag);
    CHECK(dev.calls[1].zone == EFFECT_ZONE);
}

TEST_CASE("software effect writes frames")
{
    test::TestClock clock;
    FakeController dev;
    const Config cfg;
    Engine engine(dev);
    engine.apply("sw_rainbow", cfg.params("sw_rainbow"));
    CHECK(dev.names() == std::vector<std::string>{"set_direct", "write_leds"});
    CHECK(dev.calls[0].flag);
    CHECK((engine.animated() && engine.fps() == 30));
}

TEST_CASE("audio effect crossfades to the idle effect on silence")
{
    test::TestClock clock;
    FakeController dev;
    const Config cfg;
    Engine engine(dev);
    engine.apply("sw_spectrum_color", cfg.params("sw_spectrum_color"));
    CHECK(engine.fps() == 30);                           // idle is shown at the regular frame rate during silence

    auto run = [&](double seconds, bool active) {
        for (int i = 0; i < int(seconds * 60); ++i) {
            clock.now += 1 / 60.0;
            analyzer().push({.bass = 0.8, .level = 0.8, .time = clock.now, .active = active});
            engine.tick();
        }
        return engine.idle_mix();
    };

    CHECK(engine.idle_mix() == 1.0);                              // starts in silence
    CHECK(run(0.5, true) == Catch::Approx(0.0).margin(1e-6));      // music: leave idle quickly
    CHECK(engine.fps() == 60);                                    // 60 fps under music
    CHECK(run(2.0, false) == Catch::Approx(0.0).margin(1e-6));     // a short pause keeps the music effect
    CHECK(run(2.0, false) == Catch::Approx(1.0).margin(1e-6));     // idle after 3 s of silence
    const double back = run(0.15, true);
    CHECK((back > 0.01 && back < 0.99));                          // smooth return
    CHECK(run(0.3, true) == Catch::Approx(0.0).margin(1e-6));
}

TEST_CASE("stale analyzer data counts as silence")
{
    test::TestClock clock;
    FakeController dev;
    const Config cfg;
    Engine engine(dev);
    engine.apply("sw_bass_pulse", cfg.params("sw_bass_pulse"));
    analyzer().push({.level = 1.0, .time = clock.now, .active = true});
    for (int i = 0; i < 5 * 60; ++i) {                   // data stopped arriving
        clock.now += 1 / 60.0;
        engine.tick();
    }
    CHECK(engine.idle_mix() == Catch::Approx(1.0).margin(1e-6));
}

TEST_CASE("light delay reads older features for the current output only")
{
    test::TestClock clock;
    analyzer().set_sink("bt");
    for (int i = 0; i < 100; ++i) {
        clock.now += 0.01;
        analyzer().push({.level = i / 100.0, .time = clock.now, .active = true});
    }
    CHECK(analyzer().current(clock.now).level == Catch::Approx(0.99));
    analyzer().set_delays({{"bt", 305}});                // halfway between samples, no float boundary effects
    CHECK(analyzer().current(clock.now).level == Catch::Approx(0.68));
    analyzer().set_delays({{"speakers", 305}});
    CHECK(analyzer().current(clock.now).level == Catch::Approx(0.99));
}
