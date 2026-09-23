#pragma once

// Lighting effects.
//
// Hardware ones (Kind::Hardware) run in the controller firmware and work without the daemon.
// Software ones (Kind::Software) are rendered frame by frame by the daemon in per-LED mode.
// Speed is 1..10 everywhere (10 is the fastest), brightness 0..100 %.

#include "color.hpp"
#include "device.hpp"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cougar {

using RenderFrame = std::array<Rgb, LED_COUNT>;

struct Params {
    std::string name;
    std::vector<Rgb> colors{Rgb{255, 255, 255}};
    int brightness = 100;
    int speed = 5;
    bool reverse = false;
    bool random = false;
    std::shared_ptr<const Params> idle;     // effect shown during silence (audio effects only)

    Rgb rgb(size_t i) const { return colors.empty() ? Rgb{255, 255, 255} : colors[i % colors.size()]; }
};

enum class Kind { Hardware, Software };

struct EffectInfo {
    std::string name;
    std::string title;                      // English; shown through tr()
    Kind kind = Kind::Software;
    int colors = 0;                         // number of configurable colors
    std::vector<std::string> default_colors;
    bool has_speed = true;
    bool has_random = false;
    bool has_direction = false;
    std::string speed_title = "Speed";
    bool audio = false;                     // needs audio analysis (started by the daemon only)
    int fps = 30;
};

class Effect {
public:
    explicit Effect(EffectInfo info) : info_(std::move(info)) {}
    virtual ~Effect() = default;

    const EffectInfo &info() const { return info_; }
    const std::string &name() const { return info_.name; }
    bool hardware() const { return info_.kind == Kind::Hardware; }

    virtual EffectPacket hw_packet(const Params &) const { return {}; }
    // Frame before brightness is applied; components 0..255.
    virtual RenderFrame render(double t, const Params &p) = 0;

private:
    EffectInfo info_;
};

const std::vector<std::unique_ptr<Effect>> &effects();
Effect *find_effect(std::string_view name);

inline constexpr std::string_view IDLE_NONE = "none";
inline constexpr std::string_view IDLE_DEFAULT = "sw_rainbow";
const std::vector<std::string> &idle_choices();

// Perceptual scale: 50 % on the slider looks like half the brightness.
Frame apply_brightness(const RenderFrame &frame, int brightness);

}  // namespace cougar
