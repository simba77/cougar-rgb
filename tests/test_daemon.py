from cougar_rgb import daemon


def test_resume_signal_parsing():
    prefix = '/org/freedesktop/login1: org.freedesktop.login1.Manager.PrepareForSleep '
    assert daemon.is_resume_signal(prefix + '(false,)\n')
    assert not daemon.is_resume_signal(prefix + '(true,)\n')
    assert not daemon.is_resume_signal('Monitoring signals on object /org/freedesktop/login1\n')


def test_logind_resume_forces_reconnect(monkeypatch):
    d = daemon.Daemon()
    d.dev = object()
    disconnects = []
    monkeypatch.setattr(d, 'disconnect', lambda: disconnects.append(True) or setattr(d, 'dev', None))
    monkeypatch.setattr(d, 'connect', lambda: False)
    monkeypatch.setattr(daemon.time, 'sleep', lambda s: None)

    d.sleep_watcher.resumed.set()
    d.step()
    assert disconnects == [True]
    assert not d.sleep_watcher.resumed.is_set()
