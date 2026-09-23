#include "color.hpp"

#include <cmath>
#include <cstdio>

namespace cougar {

std::optional<Rgb> parse_color(std::string_view hex)
{
    if (!hex.empty() && hex.front() == '#')
        hex.remove_prefix(1);
    if (hex.size() != 6)
        return std::nullopt;
    unsigned value = 0;
    for (char c : hex) {
        value <<= 4;
        if (c >= '0' && c <= '9')
            value |= c - '0';
        else if (c >= 'a' && c <= 'f')
            value |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            value |= c - 'A' + 10;
        else
            return std::nullopt;
    }
    return Rgb{double(value >> 16 & 0xFF), double(value >> 8 & 0xFF), double(value & 0xFF)};
}

std::string to_hex(Rgb color)
{
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", int(color.r), int(color.g), int(color.b));
    return buf;
}

Rgb hsv(double h, double s, double v)
{
    h = h - std::floor(h);
    const int i = int(h * 6.0) % 6;
    const double f = h * 6.0 - std::floor(h * 6.0);
    const double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    double r, g, b;
    switch (i) {
    case 0: r = v, g = t, b = p; break;
    case 1: r = q, g = v, b = p; break;
    case 2: r = p, g = v, b = t; break;
    case 3: r = p, g = q, b = v; break;
    case 4: r = t, g = p, b = v; break;
    default: r = v, g = p, b = q; break;
    }
    return {r * 255, g * 255, b * 255};
}

Rgb mix(Rgb a, Rgb b, double t)
{
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

Rgb scale(Rgb c, double k)
{
    return {c.r * k, c.g * k, c.b * k};
}

double seeded_random(uint64_t seed)
{
    // splitmix64
    uint64_t z = seed + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return double(z >> 11) * 0x1.0p-53;
}

}  // namespace cougar
