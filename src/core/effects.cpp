#include "effects.hpp"

#include "audio.hpp"
#include "clock.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace cougar {

namespace {

constexpr uint8_t HW_STATIC = 1, HW_PULSE = 2, HW_FLASH = 3, HW_CYCLE = 4;
constexpr double PI = std::numbers::pi;

RenderFrame fill(Rgb c)
{
    RenderFrame frame;
    frame.fill(c);
    return frame;
}

double pymod(double a, double b)
{
    return a - b * std::floor(a / b);
}

Rgb8 to_rgb8(Rgb c)
{
    auto clamp = [](double v) { return uint8_t(std::clamp(std::lround(v), 0L, 255L)); };
    return {clamp(c.r), clamp(c.g), clamp(c.b)};
}

int clamp_speed(const Params &p)
{
    return std::clamp(p.speed, 1, 10);
}

// 0 is the fastest, 9 the slowest, as in RGB Fusion
int hw_speed(const Params &p)
{
    return 10 - clamp_speed(p);
}

// Smooth speed scale: speed=1 -> slow, speed=10 -> fast (geometric).
double rate(const Params &p, double slow, double fast)
{
    const double k = (clamp_speed(p) - 1) / 9.0;
    return slow * std::pow(fast / slow, k);
}

uint8_t hw_brightness(const Params &p)
{
    return uint8_t(std::lround(std::clamp(p.brightness, 0, 100) * 255 / 100.0));
}

int direction(const Params &p)
{
    return p.reverse ? -1 : 1;
}

// ------------------------------------------------------------------ hardware

struct HwOff : Effect {
    using Effect::Effect;
    EffectPacket hw_packet(const Params &) const override { return {.type = HW_STATIC}; }
    RenderFrame render(double, const Params &) override { return fill({}); }
};

struct HwStatic : Effect {
    using Effect::Effect;
    EffectPacket hw_packet(const Params &p) const override
    {
        return {.type = HW_STATIC, .color = to_rgb8(p.rgb(0)), .max_brightness = hw_brightness(p)};
    }
    RenderFrame render(double, const Params &p) override { return fill(p.rgb(0)); }
};

struct HwPulse : Effect {
    using Effect::Effect;
    static int period(const Params &p)
    {
        const int s = hw_speed(p);
        return s <= 6 ? 400 + s * 100 : 1000 + (s - 6) * 200;
    }
    EffectPacket hw_packet(const Params &p) const override
    {
        const auto ms = uint16_t(period(p));
        return {.type = HW_PULSE, .color = to_rgb8(p.rgb(0)), .max_brightness = hw_brightness(p),
                .periods = {ms, ms, 200, 0}, .params = {uint8_t(p.random ? 7 : 0), 0, 0, 0}};
    }
    RenderFrame render(double t, const Params &p) override
    {
        const double sec = period(p) / 1000.0, cycle = 2 * sec + 0.2;
        const double n = std::floor(t / cycle), x = t - n * cycle;
        const double k = x < sec ? x / sec : std::max(0.0, 2 - x / sec);
        const Rgb color = p.random ? hsv(n / 7) : p.rgb(0);
        return fill(scale(color, std::min(1.0, k)));
    }
};

struct HwFlash : Effect {
    int count;
    HwFlash(EffectInfo info, int flashes) : Effect(std::move(info)), count(flashes) {}
    EffectPacket hw_packet(const Params &p) const override
    {
        const uint8_t rnd = p.random ? 7 : 0;
        return {.type = HW_FLASH, .color = to_rgb8(p.rgb(0)), .max_brightness = hw_brightness(p),
                .periods = {100, 100, uint16_t(hw_speed(p) * 200 + 700), 0},
                .params = count == 2 ? std::array<uint8_t, 4>{rnd, 1, 2, 0} : std::array<uint8_t, 4>{rnd, 0, 0, 0}};
    }
    RenderFrame render(double t, const Params &p) override
    {
        const double cycle = (hw_speed(p) * 200 + 700) / 1000.0 + 0.2 * count;
        const double n = std::floor(t / cycle), x = t - n * cycle;
        bool on = false;
        for (int i = 0; i < count; ++i)
            on = on || (i * 0.4 <= x && x < i * 0.4 + 0.2);
        const Rgb color = p.random ? hsv(n / 7) : p.rgb(0);
        return fill(on ? color : Rgb{});
    }
};

struct HwCycle : Effect {
    using Effect::Effect;
    static int period(const Params &p)
    {
        const int s = hw_speed(p);
        return s * 100 + 300 + (s > 8 ? 1300 * (s - 8) : 0);
    }
    EffectPacket hw_packet(const Params &p) const override
    {
        const auto ms = uint16_t(period(p));
        return {.type = HW_CYCLE, .max_brightness = hw_brightness(p),
                .periods = {ms, uint16_t(ms - 200), 0, 0}, .params = {7, 0, 0, 0}};
    }
    RenderFrame render(double t, const Params &p) override
    {
        const double step = (2 * period(p) - 200) / 1000.0;
        return fill(hsv(t / step / 7));
    }
};

// ------------------------------------------------------------------ software

struct SwRainbow : Effect {
    using Effect::Effect;
    RenderFrame render(double t, const Params &p) override
    {
        const double shift = t * rate(p, 0.05, 1.5) * direction(p);
        RenderFrame frame;
        for (int i = 0; i < LED_COUNT; ++i)
            frame[i] = hsv(double(i) / LED_COUNT - shift);
        return frame;
    }
};

struct SwSpectrum : Effect {
    using Effect::Effect;
    RenderFrame render(double t, const Params &p) override { return fill(hsv(t * rate(p, 0.01, 0.5))); }
};

struct SwBreathing : Effect {
    using Effect::Effect;
    RenderFrame render(double t, const Params &p) override
    {
        const double period = 1 / rate(p, 1 / 8.0, 1 / 1.2);
        const double n = std::floor(t / period), x = t - n * period;
        const double k = (1 - std::cos(2 * PI * x / period)) / 2;
        const Rgb color = p.random ? hsv(seeded_random(uint64_t(n))) : p.rgb(size_t(n));
        return fill(scale(color, k * k));
    }
};

struct SwComet : Effect {
    using Effect::Effect;
    static constexpr double TAIL = 3.0;
    RenderFrame render(double t, const Params &p) override
    {
        const double head = pymod(t * rate(p, 0.1, 3.0) * direction(p) * LED_COUNT, LED_COUNT);
        const Rgb fg = p.rgb(0), bg = p.rgb(1);
        RenderFrame frame;
        for (int i = 0; i < LED_COUNT; ++i) {
            const double behind = pymod((head - i) * direction(p), LED_COUNT);
            const double k = std::pow(std::max(0.0, 1 - behind / TAIL), 2);
            frame[i] = mix(bg, fg, k);
        }
        return frame;
    }
};

struct SwGradient : Effect {
    using Effect::Effect;
    RenderFrame render(double t, const Params &p) override
    {
        const double shift = t * rate(p, 0.03, 1.0) * direction(p);
        RenderFrame frame;
        for (int i = 0; i < LED_COUNT; ++i)
            frame[i] = mix(p.rgb(0), p.rgb(1), (1 - std::cos(2 * PI * (double(i) / LED_COUNT - shift))) / 2);
        return frame;
    }
};

struct SwFire : Effect {
    using Effect::Effect;
    RenderFrame render(double t, const Params &p) override
    {
        const Rgb base = p.rgb(0);
        const double r = rate(p, 0.3, 2.5);
        RenderFrame frame;
        for (int i = 0; i < LED_COUNT; ++i) {
            const double flicker = (std::sin(t * 7.1 * r + i * 1.7) + std::sin(t * 13.3 * r + i * 4.1)) / 4 + 0.5;
            const double k = 0.35 + 0.65 * flicker;
            frame[i] = {base.r * k, base.g * k * k, base.b * k * k * k};
        }
        return frame;
    }
};

// Each LED independently and smoothly fades to a new random color.
struct SwRandom : Effect {
    using Effect::Effect;
    static constexpr double FADE = 0.4;     // share of the period spent fading
    static Rgb color(int i, long n) { return hsv(seeded_random(uint64_t(i) * 100003 + uint64_t(n))); }
    RenderFrame render(double t, const Params &p) override
    {
        const double period = 1 / rate(p, 1 / 6.0, 1 / 0.4);
        RenderFrame frame;
        for (int i = 0; i < LED_COUNT; ++i) {
            const double phase = seeded_random(uint64_t(i) + 0xC0FFEE) * period;  // LEDs do not change at the same time
            const double n = std::floor((t + phase) / period), x = t + phase - n * period;
            const double k = std::max(0.0, (x / period - (1 - FADE)) / FADE);
            frame[i] = mix(color(i, long(n)), color(i, long(n) + 1), k);
        }
        return frame;
    }
};

struct SwCustom : Effect {
    using Effect::Effect;
    RenderFrame render(double, const Params &p) override
    {
        RenderFrame frame;
        for (int i = 0; i < LED_COUNT; ++i)
            frame[i] = p.rgb(i);
        return frame;
    }
};

// Music effect. Without the analyzer (in the GUI preview) it simulates a 120 BPM beat.
struct AudioEffect : Effect {
    explicit AudioEffect(EffectInfo info) : Effect(with_audio(std::move(info))) {}

    static EffectInfo with_audio(EffectInfo info)
    {
        info.audio = true;
        info.fps = 60;
        return info;
    }

    double last_t = 0;

    virtual void reset() = 0;

    double step(double t)
    {
        double dt = t - last_t;
        last_t = t;
        if (!(0 <= dt && dt < 0.2)) {
            reset();
            dt = 0;
        }
        return dt;
    }

    static Features features(double t)
    {
        if (analyzer().running())
            return analyzer().current(clock::now());
        const double x = pymod(t, 0.5);
        return Features{.bass = std::exp(-x * 8), .mid = 0.5 + 0.5 * std::sin(t * 0.7),
                        .treble = 0.5 - 0.5 * std::sin(t * 0.7), .level = 0.4 + 0.5 * std::exp(-x * 4),
                        .beats = long(t * 2), .time = t, .active = true};
    }
};

// Flash on each bass hit with a decay; between hits the lighting breathes with the bass level.
struct SwBassPulse : AudioEffect {
    using AudioEffect::AudioEffect;
    double env = 0;
    std::optional<long> beats;
    long n = 0;

    void reset() override { env = 0, beats.reset(), n = 0; }

    RenderFrame render(double t, const Params &p) override
    {
        const double dt = step(t);
        const Features f = features(t);
        const double release = 1 / rate(p, 1 / 0.9, 1 / 0.12);
        env *= std::exp(-dt / release);
        if (!beats || f.beats != *beats) {
            if (beats)
                env = 1.0, ++n;
            beats = f.beats;
        }
        env = std::max(env, f.bass * f.bass * 0.6);
        const Rgb color = p.random ? hsv(seeded_random(uint64_t(n))) : p.rgb(0);
        return fill(scale(color, env));
    }
};

// Hue from the frequency balance: bass is red, mids green/cyan, highs violet.
struct SwSpectrumColor : AudioEffect {
    using AudioEffect::AudioEffect;
    double hue = 0, level = 0;

    void reset() override { hue = 0, level = 0; }

    RenderFrame render(double t, const Params &p) override
    {
        const double dt = step(t);
        const Features f = features(t);
        const double total = f.bass + f.mid + f.treble;
        if (total > 1e-3) {
            const double target = 0.75 * (0.5 * f.mid + f.treble) / total;
            hue += (target - hue) * (1 - std::exp(-dt / (1 / rate(p, 1 / 0.6, 1 / 0.05))));
        }
        level = std::max(f.level, level * std::exp(-dt / 0.25));
        return fill(hsv(hue, 1.0, std::pow(level, 1.5)));
    }
};

std::vector<std::string> rainbow_colors()
{
    std::vector<std::string> colors;
    for (int i = 0; i < LED_COUNT; ++i)
        colors.push_back(to_hex(hsv(double(i) / LED_COUNT)));
    return colors;
}

std::vector<std::unique_ptr<Effect>> make_effects()
{
    using K = Kind;
    std::vector<std::unique_ptr<Effect>> list;
    auto add = [&](auto effect) { list.push_back(std::move(effect)); };
    add(std::make_unique<HwStatic>(EffectInfo{.name = "static", .title = "Статичный цвет", .kind = K::Hardware,
                                              .colors = 1, .default_colors = {"#ffffff"}, .has_speed = false}));
    add(std::make_unique<HwPulse>(EffectInfo{.name = "breathing", .title = "Дыхание", .kind = K::Hardware,
                                             .colors = 1, .default_colors = {"#00a0ff"}, .has_random = true}));
    add(std::make_unique<HwFlash>(EffectInfo{.name = "flash", .title = "Вспышки", .kind = K::Hardware, .colors = 1,
                                             .default_colors = {"#ff0000"}, .has_random = true}, 1));
    add(std::make_unique<HwFlash>(EffectInfo{.name = "double_flash", .title = "Двойные вспышки", .kind = K::Hardware,
                                             .colors = 1, .default_colors = {"#ff0000"}, .has_random = true}, 2));
    add(std::make_unique<HwCycle>(EffectInfo{.name = "color_cycle", .title = "Смена цветов", .kind = K::Hardware}));
    add(std::make_unique<HwOff>(EffectInfo{.name = "off", .title = "Выключено", .kind = K::Hardware,
                                           .has_speed = false}));

    add(std::make_unique<SwRainbow>(EffectInfo{.name = "sw_rainbow", .title = "Вращающаяся радуга",
                                               .has_direction = true}));
    add(std::make_unique<SwSpectrum>(EffectInfo{.name = "sw_spectrum", .title = "Плавный спектр"}));
    add(std::make_unique<SwBreathing>(EffectInfo{.name = "sw_breathing", .title = "Плавное дыхание", .colors = 3,
                                                 .default_colors = {"#ff0040", "#00a0ff", "#40ff00"},
                                                 .has_random = true}));
    add(std::make_unique<SwComet>(EffectInfo{.name = "sw_comet", .title = "Комета", .colors = 2,
                                             .default_colors = {"#00ffff", "#000010"}, .has_direction = true}));
    add(std::make_unique<SwGradient>(EffectInfo{.name = "sw_gradient", .title = "Вращающийся градиент", .colors = 2,
                                                .default_colors = {"#ff00c0", "#0060ff"}, .has_direction = true}));
    add(std::make_unique<SwFire>(EffectInfo{.name = "sw_fire", .title = "Пламя", .colors = 1,
                                            .default_colors = {"#ff6000"}}));
    add(std::make_unique<SwRandom>(EffectInfo{.name = "sw_random", .title = "Случайные цвета"}));
    add(std::make_unique<SwBassPulse>(EffectInfo{.name = "sw_bass_pulse", .title = "Пульс по басу", .colors = 1,
                                                 .default_colors = {"#ff0030"}, .has_random = true,
                                                 .speed_title = "Затухание"}));
    add(std::make_unique<SwSpectrumColor>(EffectInfo{.name = "sw_spectrum_color", .title = "Цвет по спектру",
                                                     .speed_title = "Реакция"}));
    add(std::make_unique<SwCustom>(EffectInfo{.name = "sw_custom", .title = "Свои цвета по диодам",
                                              .colors = LED_COUNT, .default_colors = rainbow_colors(),
                                              .has_speed = false}));
    return list;
}

}  // namespace

const std::vector<std::unique_ptr<Effect>> &effects()
{
    static const auto list = make_effects();
    return list;
}

Effect *find_effect(std::string_view name)
{
    for (const auto &e : effects())
        if (e->name() == name)
            return e.get();
    return nullptr;
}

const std::vector<std::string> &idle_choices()
{
    static const auto choices = [] {
        std::vector<std::string> list{std::string(IDLE_NONE)};
        for (const auto &e : effects())
            if (!e->hardware() && !e->info().audio)
                list.push_back(e->name());
        return list;
    }();
    return choices;
}

Frame apply_brightness(const RenderFrame &frame, int brightness)
{
    const double k = std::pow(std::clamp(brightness, 0, 100) / 100.0, 2.2);
    Frame out;
    for (int i = 0; i < LED_COUNT; ++i)
        out[i] = to_rgb8(scale(frame[i], k));
    return out;
}

}  // namespace cougar
