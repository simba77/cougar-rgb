#pragma once

// Эффекты подсветки.
//
// Аппаратные (Kind::Hardware) исполняет прошивка контроллера — работают без запущенной службы.
// Программные (Kind::Software) рисуются кадр за кадром службой в попиксельном режиме.
// Скорость везде 1..10 (10 — быстрее всего), яркость 0..100 %.

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
    std::shared_ptr<const Params> idle;     // эффект для тишины (только у музыкальных)

    Rgb rgb(size_t i) const { return colors.empty() ? Rgb{255, 255, 255} : colors[i % colors.size()]; }
};

enum class Kind { Hardware, Software };

struct EffectInfo {
    std::string name;
    std::string title;
    Kind kind = Kind::Software;
    int colors = 0;                         // сколько цветов настраивается
    std::vector<std::string> default_colors;
    bool has_speed = true;
    bool has_random = false;
    bool has_direction = false;
    std::string speed_title = "Скорость";
    bool audio = false;                     // нужен анализ звука (запускается только службой)
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
    // Кадр до применения яркости; компоненты 0..255.
    virtual RenderFrame render(double t, const Params &p) = 0;

private:
    EffectInfo info_;
};

const std::vector<std::unique_ptr<Effect>> &effects();
Effect *find_effect(std::string_view name);

inline constexpr std::string_view IDLE_NONE = "none";
inline constexpr std::string_view IDLE_DEFAULT = "sw_rainbow";
const std::vector<std::string> &idle_choices();

// Перцептивная шкала: 50 % на слайдере выглядит как половина яркости.
Frame apply_brightness(const RenderFrame &frame, int brightness);

}  // namespace cougar
