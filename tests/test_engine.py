import pytest

from cougar_rgb import audio
from cougar_rgb import engine as engine_module
from cougar_rgb.config import Config
from cougar_rgb.device import EFFECT_ZONE
from cougar_rgb.engine import Engine


@pytest.fixture
def clock(monkeypatch):
    now = [1000.0]
    monkeypatch.setattr(engine_module.time, 'monotonic', lambda: now[0])
    monkeypatch.setattr(audio.time, 'monotonic', lambda: now[0])
    return now


def test_setup_blanks_other_zones(fake_device):
    Engine(fake_device).setup()
    zones = [args[0] for name, args, _ in fake_device.calls if name == 'set_effect']
    assert EFFECT_ZONE not in zones and len(zones) == 7
    assert fake_device.names()[-1] == 'apply'


def test_hardware_effect_leaves_direct_mode(fake_device):
    cfg = Config()
    Engine(fake_device).apply('static', cfg.params('static'))
    assert fake_device.names() == ['set_direct', 'set_effect', 'apply']
    assert fake_device.calls[0][1] == (False,)
    assert fake_device.calls[1][1] == (EFFECT_ZONE,)


def test_software_effect_writes_frames(fake_device):
    cfg = Config()
    engine = Engine(fake_device)
    engine.apply('sw_rainbow', cfg.params('sw_rainbow'))
    assert fake_device.calls[0] == ('set_direct', (True,), {})
    assert fake_device.names()[1:] == ['write_leds']
    assert engine.animated and engine.fps == 30


def test_audio_effect_crossfades_to_idle_on_silence(fake_device, clock):
    cfg = Config()
    engine = Engine(fake_device)
    engine.apply('sw_spectrum_color', cfg.params('sw_spectrum_color'))
    assert engine.fps == 60

    def run(seconds, active):
        for _ in range(int(seconds * 60)):
            clock[0] += 1 / 60
            audio.analyzer._set(audio.Features(bass=0.8, level=0.8, time=clock[0], active=active))
            engine.tick()
        return round(engine.idle_k, 2)

    assert engine.idle_k == 1.0                   # стартуем в тишине
    assert run(0.5, True) == 0.0                  # музыка — быстро уходим с idle
    assert run(2.0, False) == 0.0                 # короткая пауза idle не включает
    assert run(2.0, False) == 1.0                 # после 3 с тишины — idle
    assert 0 < run(0.15, True) < 1                # плавный возврат
    assert run(0.3, True) == 0.0


def test_stale_analyzer_counts_as_silence(fake_device, clock):
    cfg = Config()
    engine = Engine(fake_device)
    engine.apply('sw_bass_pulse', cfg.params('sw_bass_pulse'))
    audio.analyzer._set(audio.Features(level=1.0, time=clock[0], active=True))
    for _ in range(5 * 60):                       # данные перестали приходить
        clock[0] += 1 / 60
        engine.tick()
    assert engine.idle_k == 1.0


def test_light_delay_reads_older_features(clock):
    analyzer = audio.analyzer
    analyzer._sink = 'bt'
    for i in range(100):
        clock[0] += 0.01
        analyzer._set(audio.Features(level=i / 100, time=clock[0], active=True))
    assert analyzer.current().level == pytest.approx(0.99)
    analyzer.delays = {'bt': 305}                 # середина между отсчётами, без граничных эффектов float
    assert analyzer.current().level == pytest.approx(0.68)
    analyzer.delays = {'speakers': 305}
    assert analyzer.current().level == pytest.approx(0.99)
