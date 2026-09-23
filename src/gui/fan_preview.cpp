#include "fan_preview.hpp"

#include <QPainter>
#include <QRadialGradient>
#include <cmath>
#include <numbers>

namespace cougar {

FanPreview::FanPreview(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(220, 220);
}

void FanPreview::setFrame(const Frame &frame)
{
    if (frame == frame_)
        return;
    frame_ = frame;
    update();
}

void FanPreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double side = std::min(width(), height());
    const QPointF c(width() / 2.0, height() / 2.0);
    const double radius = side * 0.36;

    p.setPen(QPen(QColor(70, 70, 70), side * 0.03));
    p.setBrush(QColor(20, 20, 20));
    p.drawEllipse(c, side * 0.45, side * 0.45);
    p.setBrush(QColor(35, 35, 35));
    p.drawEllipse(c, side * 0.12, side * 0.12);
    p.setPen(Qt::NoPen);

    for (int i = 0; i < LED_COUNT; ++i) {
        const double a = (90.0 + 360.0 / LED_COUNT * i) * std::numbers::pi / 180;
        const QPointF pos(c.x() + radius * std::sin(a), c.y() - radius * std::cos(a));
        const Rgb8 &led = frame_[i];
        QRadialGradient glow(pos, side * 0.13);
        glow.setColorAt(0, QColor(led.r, led.g, led.b, 255));
        glow.setColorAt(1, QColor(led.r, led.g, led.b, 0));
        p.setBrush(glow);
        p.drawEllipse(pos, side * 0.13, side * 0.13);
    }
}

}  // namespace cougar
