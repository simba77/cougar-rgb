#include "effects.hpp"
#include "fakes.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <set>

using namespace cougar;

namespace {

Params params_for(const Effect &e, int speed = 5)
{
    Params p;
    p.name = e.name();
    p.colors.clear();
    for (const auto &c : e.info().default_colors)
        p.colors.push_back(*parse_color(c));
    if (p.colors.empty())
        p.colors.push_back({255, 255, 255});
    p.speed = speed;
    p.random = e.info().has_random;
    return p;
}

}  // namespace

TEST_CASE("every effect renders valid frames")
{
    test::TestClock clock;
    const int speed = GENERATE(1, 5, 10);
    for (const auto &e : effects()) {
        const Params p = params_for(*e, speed);
        for (double t : {0.0, 0.016, 0.5, 3.3, 100.0}) {
            const RenderFrame frame = e->render(t, p);
            for (const Rgb &c : frame) {
                INFO(e->name() << " t=" << t);
                CHECK((c.r >= -1e-9 && c.r <= 255 + 1e-9 && c.g >= -1e-9 && c.g <= 255 + 1e-9 &&
                       c.b >= -1e-9 && c.b <= 255 + 1e-9));
            }
        }
    }
}

TEST_CASE("hardware brightness uses the full byte range")
{
    const Effect &e = *find_effect("static");
    Params p = params_for(e);
    p.brightness = 100;
    CHECK(e.hw_packet(p).max_brightness == 255);
    p.brightness = 0;
    CHECK(e.hw_packet(p).max_brightness == 0);
}

TEST_CASE("hardware breathing speed matches RGB Fusion ranges")
{
    const Effect &e = *find_effect("breathing");
    CHECK(e.hw_packet(params_for(e, 10)).periods[0] == 400);
    CHECK(e.hw_packet(params_for(e, 1)).periods[0] == 1600);
}

TEST_CASE("hardware effect colors are passed through")
{
    const Effect &e = *find_effect("static");
    Params p = params_for(e);
    p.colors = {{255, 16, 1}};
    CHECK(e.hw_packet(p).color == Rgb8{255, 16, 1});
}

TEST_CASE("apply_brightness extremes")
{
    RenderFrame frame;
    frame.fill({255, 128, 1});
    Frame full;
    full.fill({255, 128, 1});
    CHECK(apply_brightness(frame, 100) == full);
    CHECK(apply_brightness(frame, 0) == Frame{});
}

TEST_CASE("color helpers roundtrip")
{
    const auto c = parse_color("#1a2b3c");
    REQUIRE(c);
    CHECK((c->r == 0x1A && c->g == 0x2B && c->b == 0x3C));
    CHECK(to_hex(*c) == "#1a2b3c");
    CHECK(parse_color("1A2B3C"));
    CHECK_FALSE(parse_color("#12345"));
    CHECK_FALSE(parse_color("#12345g"));
}

TEST_CASE("idle choices are plain software effects")
{
    const auto &choices = idle_choices();
    CHECK(choices.front() == IDLE_NONE);
    for (size_t i = 1; i < choices.size(); ++i) {
        const Effect *e = find_effect(choices[i]);
        REQUIRE(e);
        CHECK((!e->hardware() && !e->info().audio));
    }
}

TEST_CASE("audio effects preview a simulated beat without the analyzer")
{
    for (const auto &e : effects()) {
        if (!e->info().audio)
            continue;
        const Params p = params_for(*e);
        std::set<std::tuple<int, int, int>> distinct;
        for (int i = 0; i < 120; ++i) {
            const Frame f = apply_brightness(e->render(i / 60.0, p), 100);
            distinct.insert({f[0].r, f[0].g, f[0].b});
        }
        INFO(e->name());
        CHECK(distinct.size() > 1);
    }
}
