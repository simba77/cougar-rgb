#pragma once

// Applies effects to the controller.

#include "device.hpp"
#include "effects.hpp"

#include <limits>
#include <map>
#include <optional>
#include <string>

namespace cougar {

inline constexpr double IDLE_DELAY = 3.0;       // seconds of silence before switching to the idle effect
inline constexpr double IDLE_FADE_IN = 1.0;     // fade into the idle effect
inline constexpr double IDLE_FADE_OUT = 0.3;    // fade back to the music
inline constexpr double STALE_AUDIO = 0.5;      // no fresh analyzer data for longer than this counts as silence

class Engine {
public:
    explicit Engine(Controller &device) : dev_(device) {}

    // Initial setup after connecting: turns off the other board zones.
    void setup();
    void apply(const std::string &effect_name, const Params &params,
               const std::map<std::string, int> &audio_delays = {});
    void tick();

    bool animated() const { return effect_ && !effect_->hardware(); }
    // During silence, while the idle effect is fully shown, the regular 30 fps is enough.
    int fps() const { return !effect_ ? 30 : effect_->info().audio && idle_k_ >= 1.0 ? 30 : effect_->info().fps; }
    double idle_mix() const { return idle_k_; }

private:
    RenderFrame blend_idle(const RenderFrame &frame, double t, double now, double dt);

    Controller &dev_;
    Effect *effect_ = nullptr;
    Params params_;
    double started_ = 0, last_tick_ = 0;
    double last_sound_ = -std::numeric_limits<double>::infinity();
    double idle_k_ = 1.0;                       // 0 is the music effect, 1 the idle effect
    std::optional<Frame> last_frame_;
};

}  // namespace cougar
