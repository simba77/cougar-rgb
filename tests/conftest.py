import pytest

from cougar_rgb import audio


class FakeDevice:
    """Записывает вызовы вместо общения с контроллером."""

    path = '/dev/hidraw-test'

    def __init__(self):
        self.calls = []

    def __getattr__(self, name):
        def record(*args, **kwargs):
            self.calls.append((name, args, kwargs))
        return record

    def names(self):
        return [name for name, _, _ in self.calls]


@pytest.fixture
def fake_device():
    return FakeDevice()


@pytest.fixture(autouse=True)
def no_audio_capture(monkeypatch):
    """Движок не должен запускать parec в тестах."""
    monkeypatch.setattr(audio.analyzer, 'start', lambda: None)
    monkeypatch.setattr(audio.analyzer, 'stop', lambda: None)
    audio.analyzer.features = audio.Features()
    audio.analyzer.delays = {}
    audio.analyzer._history.clear()
