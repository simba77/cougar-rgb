#include "main_window.hpp"

#include "audio.hpp"
#include "fan_preview.hpp"
#include "paths.hpp"
#include "sink.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QProcess>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace cougar {

ColorButton::ColorButton(QWidget *parent) : QPushButton(parent)
{
    setFixedSize(36, 28);
    connect(this, &QPushButton::clicked, this, &ColorButton::pick);
}

void ColorButton::setColor(const QString &color)
{
    color_ = color;
    setStyleSheet(QString("QPushButton { background:%1; border:1px solid #888; border-radius:4px; }").arg(color));
}

void ColorButton::pick()
{
    // parent is the window, not the button, otherwise the dialog inherits its background
    const QColor color = QColorDialog::getColor(QColor(color_), window(), "Цвет");
    if (color.isValid()) {
        setColor(color.name());
        emit colorChanged();
    }
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), cfg_(Config::load(paths::config_file()))
{
    setWindowTitle("Cougar RGB");
    started_ = std::chrono::steady_clock::now();

    effects_ = new QListWidget;
    effects_->setMinimumWidth(230);
    for (bool hw : {true, false}) {
        auto *header = new QListWidgetItem(hw ? "Аппаратные" : "Программные (нужна служба)");
        header->setFlags(Qt::NoItemFlags);
        QFont font = header->font();
        font.setBold(true);
        header->setFont(font);
        effects_->addItem(header);
        for (const auto &e : cougar::effects()) {
            if (e->hardware() != hw)
                continue;
            auto *item = new QListWidgetItem("   " + QString::fromStdString(e->info().title));
            item->setData(Qt::UserRole, QString::fromStdString(e->name()));
            effects_->addItem(item);
        }
    }
    connect(effects_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) { effectSelected(item); });

    preview_ = new FanPreview;

    auto *colorsRow = new QHBoxLayout;
    for (int i = 0; i < LED_COUNT; ++i) {
        auto *button = new ColorButton;
        connect(button, &ColorButton::colorChanged, this, &MainWindow::changed);
        colorButtons_.push_back(button);
        colorsRow->addWidget(button);
    }
    colorsRow->addStretch();
    colorsLabel_ = new QLabel("Цвета");

    brightness_ = makeSlider(0, 100);
    brightnessValue_ = new QLabel;
    speed_ = makeSlider(1, 10);
    speedValue_ = new QLabel;
    speedLabel_ = new QLabel("Скорость");
    reverse_ = new QCheckBox("Обратное направление");
    connect(reverse_, &QCheckBox::toggled, this, &MainWindow::changed);
    random_ = new QCheckBox("Случайные цвета");
    connect(random_, &QCheckBox::toggled, this, &MainWindow::changed);

    idle_ = new QComboBox;
    for (const auto &name : idle_choices())
        idle_->addItem(name == IDLE_NONE ? QString("Выключено") : QString::fromStdString(find_effect(name)->info().title),
                       QString::fromStdString(name));
    connect(idle_, &QComboBox::currentIndexChanged, this, &MainWindow::changed);
    idleLabel_ = new QLabel("Когда тихо");

    if (const auto sink = default_sink())
        sink_ = sink->name;
    delay_ = makeSlider(0, int(MAX_DELAY * 1000));
    delay_->setSingleStep(10);
    delay_->setPageStep(50);
    delayValue_ = new QLabel;
    delayLabel_ = new QLabel("Задержка света");
    delayHint_ = new QLabel;
    delayHint_->setStyleSheet("color: gray");
    delayHint_->setWordWrap(true);

    auto *form = new QFormLayout;
    form->addRow(colorsLabel_, colorsRow);
    form->addRow("Яркость", withValue(brightness_, brightnessValue_));
    speedRow_ = withValue(speed_, speedValue_);
    form->addRow(speedLabel_, speedRow_);
    form->addRow("", reverse_);
    form->addRow("", random_);
    form->addRow(idleLabel_, idle_);
    delayRow_ = withValue(delay_, delayValue_);
    form->addRow(delayLabel_, delayRow_);
    form->addRow("", delayHint_);
    auto *params = new QGroupBox("Параметры");
    params->setLayout(form);

    status_ = new QLabel;
    startButton_ = new QPushButton("Запустить службу");
    connect(startButton_, &QPushButton::clicked, this, &MainWindow::startDaemon);
    auto *statusRow = new QHBoxLayout;
    statusRow->addWidget(status_, 1);
    statusRow->addWidget(startButton_);

    auto *right = new QVBoxLayout;
    right->addWidget(preview_, 1);
    right->addWidget(params);
    right->addLayout(statusRow);

    auto *root = new QHBoxLayout;
    root->addWidget(effects_);
    root->addLayout(right, 1);
    auto *central = new QWidget;
    central->setLayout(root);
    setCentralWidget(central);
    resize(720, 560);

    saveTimer_ = new QTimer(this);
    saveTimer_->setSingleShot(true);
    saveTimer_->setInterval(120);
    connect(saveTimer_, &QTimer::timeout, this, &MainWindow::save);
    auto *animation = new QTimer(this);
    connect(animation, &QTimer::timeout, this, &MainWindow::animate);
    animation->start(33);
    auto *status = new QTimer(this);
    connect(status, &QTimer::timeout, this, &MainWindow::updateStatus);
    status->start(2000);

    selectEffect(cfg_.effect);
    updateStatus();
}

QSlider *MainWindow::makeSlider(int lo, int hi)
{
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(lo, hi);
    connect(slider, &QSlider::valueChanged, this, &MainWindow::changed);
    return slider;
}

QWidget *MainWindow::withValue(QSlider *slider, QLabel *label)
{
    label->setMinimumWidth(48);
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(slider, 1);
    row->addWidget(label);
    auto *widget = new QWidget;
    widget->setLayout(row);
    return widget;
}

void MainWindow::selectEffect(const std::string &name)
{
    for (int i = 0; i < effects_->count(); ++i)
        if (effects_->item(i)->data(Qt::UserRole).toString().toStdString() == name) {
            effects_->setCurrentRow(i);
            return;
        }
}

void MainWindow::effectSelected(QListWidgetItem *item)
{
    const QString name = item ? item->data(Qt::UserRole).toString() : QString();
    if (name.isEmpty())
        return;
    cfg_.effect = name.toStdString();
    loadControls();
    changed();
}

void MainWindow::loadControls()
{
    const Effect &effect = *find_effect(cfg_.effect);
    const EffectOptions &opts = cfg_.options();
    loading_ = true;
    for (int i = 0; i < LED_COUNT; ++i) {
        colorButtons_[i]->setVisible(i < effect.info().colors);
        if (i < effect.info().colors && size_t(i) < opts.colors.size())
            colorButtons_[i]->setColor(QString::fromStdString(opts.colors[i]));
    }
    colorsLabel_->setVisible(effect.info().colors > 0);
    brightness_->setValue(cfg_.brightness);
    speed_->setValue(opts.speed);
    speedRow_->setVisible(effect.info().has_speed);
    speedLabel_->setVisible(effect.info().has_speed);
    speedLabel_->setText(QString::fromStdString(effect.info().speed_title));
    reverse_->setChecked(opts.reverse);
    reverse_->setVisible(effect.info().has_direction);
    random_->setChecked(opts.random);
    random_->setVisible(effect.info().has_random);
    const bool audio = effect.info().audio;
    if (audio && opts.idle)
        idle_->setCurrentIndex(idle_->findData(QString::fromStdString(*opts.idle)));
    for (QWidget *w : std::initializer_list<QWidget *>{idle_, idleLabel_, delayRow_, delayLabel_, delayHint_})
        w->setVisible(audio);
    loading_ = false;
    loadDelay();
    started_ = std::chrono::steady_clock::now();
}

void MainWindow::loadDelay()
{
    const bool wasLoading = loading_;
    loading_ = true;
    const auto it = cfg_.audio_delays.find(sink_);
    delay_->setValue(it == cfg_.audio_delays.end() ? 0 : it->second);
    QString device = "не найдено";
    if (!sink_.empty()) {
        const auto sink = default_sink();
        device = QString::fromStdString(sink && sink->name == sink_ ? sink->description : sink_);
    }
    delayHint_->setText(QString("Для устройства вывода: %1. Нужна для Bluetooth-наушников — "
                                "свет ждёт, пока звук дойдёт до ушей.").arg(device));
    loading_ = wasLoading;
    updateLabels();
}

void MainWindow::updateLabels()
{
    brightnessValue_->setText(QString("%1%").arg(brightness_->value()));
    speedValue_->setText(QString::number(speed_->value()));
    delayValue_->setText(QString("%1 мс").arg(delay_->value()));
}

void MainWindow::changed()
{
    if (loading_)
        return;
    updateLabels();
    const Effect &effect = *find_effect(cfg_.effect);
    EffectOptions &opts = cfg_.options();
    if (effect.info().colors > 0) {
        opts.colors.clear();
        for (int i = 0; i < effect.info().colors; ++i)
            opts.colors.push_back(colorButtons_[i]->color().toStdString());
    }
    opts.speed = speed_->value();
    opts.reverse = reverse_->isChecked();
    opts.random = random_->isChecked();
    if (effect.info().audio) {
        opts.idle = idle_->currentData().toString().toStdString();
        if (!sink_.empty())
            cfg_.audio_delays[sink_] = delay_->value();
    }
    cfg_.brightness = brightness_->value();
    saveTimer_->start();
}

void MainWindow::save()
{
    cfg_.save(paths::config_file());
    if (paths::daemon_running())
        return;
    // Without the daemon apply hardware effects directly; the firmware keeps them running
    if (find_effect(cfg_.effect)->hardware()) {
        try {
            if (!direct_engine_) {
                direct_device_ = Fusion2::open();
                direct_engine_ = std::make_unique<Engine>(*direct_device_);
                direct_engine_->setup();
            }
            direct_engine_->apply(cfg_.effect, cfg_.params());
        } catch (const DeviceError &e) {
            direct_engine_.reset();
            direct_device_.reset();
            status_->setText(QString("Ошибка контроллера: %1").arg(e.what()));
            return;
        }
    }
    updateStatus();
}

double MainWindow::elapsed() const
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
}

void MainWindow::animate()
{
    Effect *effect = find_effect(cfg_.effect);
    const Params params = cfg_.params();
    // the preview gets a brightness floor, otherwise dim colors get lost on screen
    preview_->setFrame(apply_brightness(effect->render(elapsed(), params), std::max(params.brightness, 15)));
}

void MainWindow::updateStatus()
{
    const auto sink = default_sink();
    const std::string name = sink ? sink->name : std::string();
    if (name != sink_) {
        sink_ = name;
        loadDelay();
    }
    const bool running = paths::daemon_running();
    startButton_->setVisible(!running);
    if (running)
        status_->setText("Служба работает");
    else if (!find_effect(cfg_.effect)->hardware())
        status_->setText("<span style=\"color:#e06c00\">Служба не запущена — программный эффект не работает</span>");
    else
        status_->setText("Служба не запущена (аппаратный эффект применён напрямую)");
}

void MainWindow::startDaemon()
{
    direct_engine_.reset();
    direct_device_.reset();
    if (std::filesystem::exists(paths::user_service()) || std::filesystem::exists(paths::system_service()))
        QProcess::execute("systemctl", {"--user", "start", "cougar-rgb.service"});
    else
        QProcess::startDetached(QString::fromStdString(paths::sibling_executable("cougar-rgb").string()), {"daemon"});
    QTimer::singleShot(700, this, &MainWindow::updateStatus);
}

}  // namespace cougar
