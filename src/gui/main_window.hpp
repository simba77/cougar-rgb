#pragma once

// Main window. Saves settings to the config; the daemon applies them live.

#include "config.hpp"
#include "device.hpp"
#include "engine.hpp"

#include <QMainWindow>
#include <QPushButton>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QSlider;
class QTimer;

namespace cougar {

class FanPreview;

class ColorButton : public QPushButton {
    Q_OBJECT
public:
    explicit ColorButton(QWidget *parent = nullptr);
    void setColor(const QString &color);
    QString color() const { return color_; }

signals:
    void colorChanged();

private:
    void pick();
    QString color_ = "#ffffff";
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

signals:
    // The window has to be rebuilt to show the new language.
    void languageChanged();

private:
    QSlider *makeSlider(int lo, int hi);
    QWidget *withValue(QSlider *slider, QLabel *label);
    void selectEffect(const std::string &name);
    void effectSelected(QListWidgetItem *item);
    void loadControls();
    void loadDelay();
    void updateLabels();
    void changed();
    void save();
    void animate();
    void updateStatus();
    void startDaemon();
    double elapsed() const;

    Config cfg_;
    bool loading_ = true;
    std::chrono::steady_clock::time_point started_;
    std::string sink_;
    std::unique_ptr<Fusion2> direct_device_;
    std::unique_ptr<Engine> direct_engine_;

    QListWidget *effects_;
    FanPreview *preview_;
    std::vector<ColorButton *> colorButtons_;
    QLabel *colorsLabel_;
    QSlider *brightness_, *speed_, *delay_;
    QLabel *brightnessValue_, *speedValue_, *speedLabel_, *delayValue_, *delayLabel_, *delayHint_, *idleLabel_;
    QWidget *speedRow_, *delayRow_;
    QCheckBox *reverse_, *random_;
    QComboBox *idle_, *language_;
    QLabel *status_;
    QPushButton *startButton_;
    QTimer *saveTimer_;
};

}  // namespace cougar
