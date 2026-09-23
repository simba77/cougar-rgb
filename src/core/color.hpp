#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cougar {

// Цвет в процессе расчёта эффекта: компоненты 0..255, могут быть дробными.
struct Rgb {
    double r = 0, g = 0, b = 0;
};

// Цвет, готовый к отправке на контроллер.
struct Rgb8 {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb8 &) const = default;
};

std::optional<Rgb> parse_color(std::string_view hex);
std::string to_hex(Rgb color);
Rgb hsv(double h, double s = 1.0, double v = 1.0);
Rgb mix(Rgb a, Rgb b, double t);
Rgb scale(Rgb c, double k);

// Детерминированное псевдослучайное число 0..1 по зерну (вместо random.Random(seed).random()).
double seeded_random(uint64_t seed);

}  // namespace cougar
