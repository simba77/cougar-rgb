import json

from cougar_rgb.config import Config
from cougar_rgb.effects import EFFECTS, IDLE_DEFAULT


def test_defaults_cover_every_effect():
    cfg = Config()
    assert set(cfg.effects) == set(EFFECTS)
    for name, effect in EFFECTS.items():
        assert len(cfg.effects[name]['colors']) >= effect.colors
        assert ('idle' in cfg.effects[name]) == effect.audio


def test_save_and_load_roundtrip(tmp_path):
    path = tmp_path / 'cfg' / 'config.json'
    cfg = Config()
    cfg.effect = 'sw_comet'
    cfg.brightness = 42
    cfg.audio_delays = {'bt_sink': 250}
    cfg.effects['sw_comet']['colors'] = ['#010203', '#040506']
    cfg.save(path)

    loaded = Config.load(path)
    assert loaded.effect == 'sw_comet'
    assert loaded.brightness == 42
    assert loaded.audio_delays == {'bt_sink': 250}
    assert loaded.effects['sw_comet']['colors'] == ['#010203', '#040506']


def test_invalid_values_fall_back(tmp_path):
    path = tmp_path / 'config.json'
    path.write_text(json.dumps({
        'effect': 'removed_effect',
        'effects': {'sw_bass_pulse': {'idle': 'removed_effect', 'bogus': 1}},
    }))
    cfg = Config.load(path)
    assert cfg.effect in EFFECTS
    assert cfg.effects['sw_bass_pulse']['idle'] == IDLE_DEFAULT
    assert 'bogus' not in cfg.effects['sw_bass_pulse']


def test_broken_file_gives_defaults(tmp_path):
    path = tmp_path / 'config.json'
    path.write_text('{not json')
    assert Config.load(path).effect in EFFECTS


def test_audio_params_carry_idle_effect_settings():
    cfg = Config()
    cfg.effects['sw_bass_pulse']['idle'] = 'sw_fire'
    cfg.effects['sw_fire']['speed'] = 9
    p = cfg.params('sw_bass_pulse')
    assert p.idle.name == 'sw_fire' and p.idle.speed == 9

    cfg.effects['sw_bass_pulse']['idle'] = 'none'
    assert cfg.params('sw_bass_pulse').idle is None
