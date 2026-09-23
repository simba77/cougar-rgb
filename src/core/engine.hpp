#pragma once

// Применение эффекта к контроллеру.

#include "device.hpp"
#include "effects.hpp"

#include <limits>
#include <map>
#include <optional>
#include <string>

namespace cougar {

inline constexpr double IDLE_DELAY = 3.0;       // столько секунд тишины до перехода на эффект для тишины
inline constexpr double IDLE_FADE_IN = 1.0;     // переход в эффект для тишины
inline constexpr double IDLE_FADE_OUT = 0.3;    // возврат к музыке
inline constexpr double STALE_AUDIO = 0.5;      // нет свежих данных от анализатора дольше — считаем тишиной

class Engine {
public:
    explicit Engine(Controller &device) : dev_(device) {}

    // Первичная настройка после подключения: гасим остальные зоны платы.
    void setup();
    void apply(const std::string &effect_name, const Params &params,
               const std::map<std::string, int> &audio_delays = {});
    void tick();

    bool animated() const { return effect_ && !effect_->hardware(); }
    // В тишине, когда полностью показан эффект для тишины, хватает обычных 30 кадров/с.
    int fps() const { return !effect_ ? 30 : effect_->info().audio && idle_k_ >= 1.0 ? 30 : effect_->info().fps; }
    double idle_mix() const { return idle_k_; }

private:
    RenderFrame blend_idle(const RenderFrame &frame, double t, double now, double dt);

    Controller &dev_;
    Effect *effect_ = nullptr;
    Params params_;
    double started_ = 0, last_tick_ = 0;
    double last_sound_ = -std::numeric_limits<double>::infinity();
    double idle_k_ = 1.0;                       // 0 — музыкальный эффект, 1 — эффект для тишины
    std::optional<Frame> last_frame_;
};

}  // namespace cougar
