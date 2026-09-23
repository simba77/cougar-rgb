#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cougar {

// Color while an effect is being computed: components 0..255, may be fractional.
struct Rgb {
    double r = 0, g = 0, b = 0;
};

// Color ready to be sent to the controller.
struct Rgb8 {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb8 &) const = default;
};

std::optional<Rgb> parse_color(std::string_view hex);
std::string to_hex(Rgb color);
Rgb hsv(double h, double s = 1.0, double v = 1.0);
Rgb mix(Rgb a, Rgb b, double t);
Rgb scale(Rgb c, double k);

// Deterministic pseudo-random number 0..1 from a seed.
double seeded_random(uint64_t seed);

}  // namespace cougar
