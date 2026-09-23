#include "engine.hpp"

#include "audio.hpp"
#include "clock.hpp"

#include <algorithm>

namespace cougar {

void Engine::setup()
{
    dev_.init();
    for (int zone = 0; zone < ZONE_COUNT; ++zone)
        if (zone != EFFECT_ZONE)
            dev_.set_effect(zone, EffectPacket{});
    dev_.apply();
}

void Engine::apply(const std::string &effect_name, const Params &params,
                   const std::map<std::string, int> &audio_delays)
{
    Effect *effect = find_effect(effect_name);
    if (!effect)
        return;
    effect_ = effect;
    params_ = params;
    started_ = last_tick_ = clock::now();
    last_frame_.reset();
    last_sound_ = -std::numeric_limits<double>::infinity();
    idle_k_ = 1.0;

    analyzer().set_delays(audio_delays);
    if (effect->info().audio)
        analyzer().start();
    else
        analyzer().stop();

    if (effect->hardware()) {
        dev_.set_direct(false);
        dev_.set_effect(EFFECT_ZONE, effect->hw_packet(params));
        dev_.apply();
    } else {
        dev_.set_direct(true);
        tick();
    }
}

void Engine::tick()
{
    if (!animated())
        return;
    const double now = clock::now();
    const double dt = now - last_tick_;
    last_tick_ = now;
    const double t = now - started_;
    RenderFrame frame = effect_->render(t, params_);
    if (effect_->info().audio)
        frame = blend_idle(frame, t, now, dt);
    const Frame out = apply_brightness(frame, params_.brightness);
    if (out != last_frame_) {
        dev_.write_leds(out);
        last_frame_ = out;
    }
}

// Плавно подменяет музыкальный эффект эффектом для тишины, когда ничего не играет.
RenderFrame Engine::blend_idle(const RenderFrame &frame, double t, double now, double dt)
{
    const Features f = analyzer().current(now);
    if (f.active && now - analyzer().delay() - f.time < STALE_AUDIO)
        last_sound_ = now;
    const double target = now - last_sound_ > IDLE_DELAY ? 1.0 : 0.0;
    const double step = dt / (target > idle_k_ ? IDLE_FADE_IN : IDLE_FADE_OUT);
    idle_k_ += std::clamp(target - idle_k_, -step, step);
    if (idle_k_ <= 0)
        return frame;

    RenderFrame idle_frame{};
    if (params_.idle)
        if (Effect *idle = find_effect(params_.idle->name))
            idle_frame = idle->render(t, *params_.idle);
    RenderFrame out;
    for (int i = 0; i < LED_COUNT; ++i)
        out[i] = mix(frame[i], idle_frame[i], idle_k_);
    return out;
}

}  // namespace cougar
