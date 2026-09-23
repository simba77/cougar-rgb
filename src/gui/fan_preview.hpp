#pragma once

#include "device.hpp"

#include <QWidget>

namespace cougar {

// Схематичный вентилятор: светодиод 0 на 3 часах, дальше по часовой через 45°.
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
