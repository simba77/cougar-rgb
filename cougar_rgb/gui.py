"""Графический интерфейс (PySide6). Сохраняет настройки в конфиг — служба применяет их на лету."""
import math
import os
import subprocess
import sys
import time

from PySide6.QtCore import QPointF, Qt, QTimer
from PySide6.QtGui import QBrush, QColor, QPainter, QPen, QRadialGradient
from PySide6.QtWidgets import (QApplication, QCheckBox, QColorDialog, QComboBox, QFormLayout, QGroupBox, QHBoxLayout,
                               QLabel, QListWidget, QListWidgetItem, QMainWindow, QPushButton, QSlider,
                               QVBoxLayout, QWidget)

from . import audio, daemon
from .cli import SERVICE_PATH, _launch_env
from .config import Config
from .device import LED_COUNT, Fusion2
from .effects import EFFECTS, IDLE_CHOICES, IDLE_NONE, apply_brightness
from .engine import Engine


class FanPreview(QWidget):
    """Схематичный вентилятор: светодиод 0 на 3 часах, дальше по часовой через 45°."""

    def __init__(self):
        super().__init__()
        self.setMinimumSize(220, 220)
        self.frame = [(0, 0, 0)] * LED_COUNT

    def set_frame(self, frame):
        self.frame = frame
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        side = min(self.width(), self.height())
        c = QPointF(self.width() / 2, self.height() / 2)
        radius = side * 0.36
        p.setPen(QPen(QColor(70, 70, 70), side * 0.03))
        p.setBrush(QColor(20, 20, 20))
        p.drawEllipse(c, side * 0.45, side * 0.45)
        p.setBrush(QColor(35, 35, 35))
        p.drawEllipse(c, side * 0.12, side * 0.12)
        p.setPen(Qt.NoPen)
        for i, (r, g, b) in enumerate(self.frame):
            a = math.radians(90 + 360 / LED_COUNT * i)
            pos = QPointF(c.x() + radius * math.sin(a), c.y() - radius * math.cos(a))
            glow = QRadialGradient(pos, side * 0.13)
            glow.setColorAt(0, QColor(r, g, b, 255))
            glow.setColorAt(1, QColor(r, g, b, 0))
            p.setBrush(QBrush(glow))
            p.drawEllipse(pos, side * 0.13, side * 0.13)


class ColorButton(QPushButton):
    def __init__(self, on_change):
        super().__init__()
        self.setFixedSize(36, 28)
        self.color = '#ffffff'
        self.on_change = on_change
        self.clicked.connect(self.pick)

    def set_color(self, color):
        self.color = color
        self.setStyleSheet('QPushButton { background:%s; border:1px solid #888; border-radius:4px; }' % color)

    def pick(self):
        # родитель — окно, а не кнопка, иначе диалог унаследует её фон
        color = QColorDialog.getColor(QColor(self.color), self.window(), 'Цвет')
        if color.isValid():
            self.set_color(color.name())
            self.on_change()


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle('Cougar RGB')
        self.cfg = Config.load()
        self.loading = True
        self.started = time.monotonic()
        self.direct_engine = None

        self.effects = QListWidget()
        self.effects.setMinimumWidth(230)
        for kind, title in (('hw', 'Аппаратные'), ('sw', 'Программные (нужна служба)')):
            header = QListWidgetItem(title)
            header.setFlags(Qt.NoItemFlags)
            font = header.font()
            font.setBold(True)
            header.setFont(font)
            self.effects.addItem(header)
            for e in EFFECTS.values():
                if e.kind == kind:
                    item = QListWidgetItem('   ' + e.title)
                    item.setData(Qt.UserRole, e.name)
                    self.effects.addItem(item)
        self.effects.currentItemChanged.connect(self.effect_selected)

        self.preview = FanPreview()

        self.color_buttons = [ColorButton(self.changed) for _ in range(LED_COUNT)]
        colors_row = QHBoxLayout()
        for b in self.color_buttons:
            colors_row.addWidget(b)
        colors_row.addStretch()
        self.colors_label = QLabel('Цвета')

        self.brightness = self._slider(0, 100)
        self.brightness_value = QLabel()
        self.speed = self._slider(1, 10)
        self.speed_value = QLabel()
        self.speed_label = QLabel('Скорость')
        self.reverse = QCheckBox('Обратное направление')
        self.reverse.toggled.connect(self.changed)
        self.random = QCheckBox('Случайные цвета')
        self.random.toggled.connect(self.changed)
        self.idle = QComboBox()
        for name in IDLE_CHOICES:
            self.idle.addItem('Выключено' if name == IDLE_NONE else EFFECTS[name].title, name)
        self.idle.currentIndexChanged.connect(self.changed)
        self.idle_label = QLabel('Когда тихо')
        self.sink = audio.default_sink()
        self.delay = self._slider(0, int(audio.MAX_DELAY * 1000))
        self.delay.setSingleStep(10)
        self.delay.setPageStep(50)
        self.delay_value = QLabel()
        self.delay_label = QLabel('Задержка света')
        self.delay_hint = QLabel()
        self.delay_hint.setStyleSheet('color: gray')
        self.delay_hint.setWordWrap(True)

        form = QFormLayout()
        form.addRow(self.colors_label, colors_row)
        form.addRow('Яркость', self._with_value(self.brightness, self.brightness_value))
        self.speed_row = self._with_value(self.speed, self.speed_value)
        form.addRow(self.speed_label, self.speed_row)
        form.addRow('', self.reverse)
        form.addRow('', self.random)
        form.addRow(self.idle_label, self.idle)
        self.delay_row = self._with_value(self.delay, self.delay_value)
        form.addRow(self.delay_label, self.delay_row)
        form.addRow('', self.delay_hint)
        params = QGroupBox('Параметры')
        params.setLayout(form)

        self.status = QLabel()
        self.start_button = QPushButton('Запустить службу')
        self.start_button.clicked.connect(self.start_daemon)
        status_row = QHBoxLayout()
        status_row.addWidget(self.status, 1)
        status_row.addWidget(self.start_button)

        right = QVBoxLayout()
        right.addWidget(self.preview, 1)
        right.addWidget(params)
        right.addLayout(status_row)

        root = QHBoxLayout()
        root.addWidget(self.effects)
        root.addLayout(right, 1)
        central = QWidget()
        central.setLayout(root)
        self.setCentralWidget(central)
        self.resize(720, 520)

        self.save_timer = QTimer(self, singleShot=True, interval=120, timeout=self.save)
        QTimer(self, interval=33, timeout=self.animate).start()
        QTimer(self, interval=2000, timeout=self.update_status).start()

        self.select_effect(self.cfg.effect)
        self.update_status()

    def _slider(self, lo, hi):
        s = QSlider(Qt.Horizontal)
        s.setRange(lo, hi)
        s.valueChanged.connect(self.changed)
        return s

    @staticmethod
    def _with_value(slider, label):
        label.setMinimumWidth(36)
        row = QHBoxLayout()
        row.addWidget(slider, 1)
        row.addWidget(label)
        w = QWidget()
        row.setContentsMargins(0, 0, 0, 0)
        w.setLayout(row)
        return w

    def select_effect(self, name):
        for i in range(self.effects.count()):
            if self.effects.item(i).data(Qt.UserRole) == name:
                self.effects.setCurrentRow(i)
                return

    def effect_selected(self, item, _prev=None):
        name = item.data(Qt.UserRole) if item else None
        if not name:
            return
        self.cfg.effect = name
        self.load_controls()
        self.changed()

    def load_controls(self):
        effect = EFFECTS[self.cfg.effect]
        opts = self.cfg.effects[effect.name]
        self.loading = True
        for i, b in enumerate(self.color_buttons):
            b.setVisible(i < effect.colors)
            if i < effect.colors:
                b.set_color(opts['colors'][i])
        self.colors_label.setVisible(effect.colors > 0)
        self.brightness.setValue(self.cfg.brightness)
        self.speed.setValue(opts['speed'])
        self.speed_row.setVisible(effect.has_speed)
        self.speed_label.setVisible(effect.has_speed)
        self.speed_label.setText(effect.speed_title)
        self.reverse.setChecked(opts['reverse'])
        self.reverse.setVisible(effect.has_direction)
        self.random.setChecked(opts['random'])
        self.random.setVisible(effect.has_random)
        if effect.audio:
            self.idle.setCurrentIndex(self.idle.findData(opts['idle']))
        for w in (self.idle, self.idle_label, self.delay_row, self.delay_label, self.delay_hint):
            w.setVisible(effect.audio)
        self.load_delay()
        self.loading = False
        self.update_labels()
        self.started = time.monotonic()

    def load_delay(self):
        loading, self.loading = self.loading, True
        self.delay.setValue(self.cfg.audio_delays.get(self.sink, 0))
        self.delay_hint.setText('Для устройства вывода: %s. Нужна для Bluetooth-наушников — '
                                'свет ждёт, пока звук дойдёт до ушей.'
                                % (audio.sink_description(self.sink) if self.sink else 'не найдено'))
        self.loading = loading
        self.update_labels()

    def update_labels(self):
        self.brightness_value.setText('%d%%' % self.brightness.value())
        self.speed_value.setText(str(self.speed.value()))
        self.delay_value.setText('%d мс' % self.delay.value())

    def changed(self):
        if self.loading:
            return
        self.update_labels()
        effect = EFFECTS[self.cfg.effect]
        opts = self.cfg.effects[effect.name]
        opts['colors'] = [b.color for b in self.color_buttons[:effect.colors]] or opts['colors']
        opts['speed'] = self.speed.value()
        opts['reverse'] = self.reverse.isChecked()
        opts['random'] = self.random.isChecked()
        if effect.audio:
            opts['idle'] = self.idle.currentData()
        self.cfg.brightness = self.brightness.value()
        if effect.audio and self.sink:
            self.cfg.audio_delays[self.sink] = self.delay.value()
        self.save_timer.start()

    def save(self):
        self.cfg.save()
        if daemon.is_running():
            return
        # Без службы аппаратные эффекты применяем сами — прошивка дальше справится
        if EFFECTS[self.cfg.effect].kind == 'hw':
            try:
                if self.direct_engine is None:
                    self.direct_engine = Engine(Fusion2())
                    self.direct_engine.setup()
                self.direct_engine.apply(self.cfg.effect, self.cfg.params())
            except OSError as e:
                self.direct_engine = None
                self.status.setText('Ошибка контроллера: %s' % e)
        self.update_status()

    def animate(self):
        effect = EFFECTS[self.cfg.effect]
        params = self.cfg.params()
        frame = effect.render(time.monotonic() - self.started, params)
        # предпросмотр чуть приподнят по яркости, иначе на экране тусклые цвета теряются
        frame = apply_brightness(frame, max(params.brightness, 15))
        self.preview.set_frame(frame)

    def update_status(self):
        sink = audio.default_sink()
        if sink != self.sink:
            self.sink = sink
            self.load_delay()
        running = daemon.is_running()
        self.start_button.setVisible(not running)
        if running:
            text = 'Служба работает'
        elif EFFECTS[self.cfg.effect].kind == 'sw':
            text = '<span style="color:#e06c00">Служба не запущена — программный эффект не работает</span>'
        else:
            text = 'Служба не запущена (аппаратный эффект применён напрямую)'
        self.status.setText(text)

    def start_daemon(self):
        if self.direct_engine:
            self.direct_engine.dev.close()
            self.direct_engine = None
        if SERVICE_PATH.exists():
            subprocess.run(['systemctl', '--user', 'start', 'cougar-rgb.service'])
        else:
            env = dict(os.environ, **_launch_env())
            subprocess.Popen([sys.executable, '-m', 'cougar_rgb', 'daemon'], env=env, start_new_session=True,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        QTimer.singleShot(700, self.update_status)


def main():
    app = QApplication(sys.argv)
    app.setApplicationName('Cougar RGB')
    app.setDesktopFileName('cougar-rgb')
    w = MainWindow()
    w.show()
    sys.exit(app.exec())
