#pragma once

#include "device.hpp"

#include <QWidget>

namespace cougar {

// Schematic fan: LED 0 at 3 o'clock, then clockwise every 45°.
class FanPreview : public QWidget {
    Q_OBJECT
public:
    explicit FanPreview(QWidget *parent = nullptr);
    void setFrame(const Frame &frame);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Frame frame_{};
};

}  // namespace cougar
