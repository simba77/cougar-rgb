# cougar-rgb

Linux control for ARGB case lighting driven by the **Gigabyte RGB Fusion 2 USB** controller
(ITE `048d:5702`, found on many Gigabyte/AORUS motherboards). Built for a **Cougar Duoface Pro RGB**
case whose fan hub is switched to motherboard sync mode, but anything plugged into the `D_LED1`
ARGB header should work.

![GUI](docs/screenshot.png)

> The GUI and CLI messages are currently in Russian.

## Why

Existing Linux tools could switch modes, but the lighting stayed noticeably dimmer than under
Windows. The controller's effect brightness is a 0–255 byte, not a percentage; sending `100`
gives roughly 40 % brightness. This project always drives it at the full range and adds
per-LED software effects the firmware does not have.

## Features

- **Hardware effects** executed by the controller firmware: static, breathing, flash,
  double flash, color cycle, off. They keep running without any software.
- **Software per-LED effects** rendered by a small daemon: rotating rainbow, smooth spectrum,
  smooth breathing, comet, rotating gradient, fire, random colors, custom color per LED.
- **Music-reactive effects** from whatever is playing on the default output: bass pulse and
  spectrum color. Capture follows the default output when you switch devices, falls back to a
  chosen idle effect during silence, and supports a per-output light delay for Bluetooth
  headphones.
- **Daemon** (systemd user service) that applies the saved config on login, reapplies it after
  suspend or USB reconnects, and picks up config changes live.
- **GUI** (PySide6) with a live fan preview, and a **CLI** for scripting.

## Hardware

Tested on:

| Part | Model |
|---|---|
| Motherboard | Gigabyte Z790 GAMING X AX |
| Controller | ITE `048d:5702`, firmware `IT5701-GIGABYTE V3.5.5.0` |
| Case | Cougar Duoface Pro RGB (CGR-5AD1B-RGB), hub in motherboard sync mode |

What the code assumes about the setup (see `cougar_rgb/device.py` to adapt it):

- the lighting is on `D_LED1`: builtin effects use zone 5, per-LED data goes to register `0x58`,
  direct mode is bit `0x01` of command `0x32`;
- colors are sent in GRB order, both for effects and per-LED frames;
- 8 addressable LEDs; the Cougar hub mirrors them onto every fan.

The rest of the controller's zones are switched off when the daemon connects.

## Installation

Requirements: Linux, Python 3.10+, `numpy`; `PySide6` for the GUI; PipeWire or PulseAudio
with `pactl` and `parec` for the music effects.

1. Allow your user to access the controller without root:

   ```sh
   sudo cp packaging/60-rgb-fusion2.rules /etc/udev/rules.d/
   sudo udevadm control --reload && sudo udevadm trigger
   ```

2. Install the app, for example with pipx:

   ```sh
   pipx install "cougar-rgb[gui] @ git+https://github.com/simba77/cougar-rgb"
   ```

   Or run it straight from a checkout: `./cougar-rgb <command>`. It uses the system Python,
   so `numpy` (and `PySide6` for the GUI) must be available to it, from your distribution's
   packages or pip.

3. Install and start the daemon (systemd user service) and a desktop entry:

   ```sh
   cougar-rgb install
   ```

   `cougar-rgb uninstall` removes both.

## Usage

```sh
cougar-rgb gui                                   # graphical settings
cougar-rgb list                                  # available effects
cougar-rgb info                                  # controller and daemon status
cougar-rgb set static -c ff0040 -b 80            # hardware static color at 80 %
cougar-rgb set sw_rainbow -s 7 --reverse         # software rainbow, faster, counterclockwise
cougar-rgb set sw_bass_pulse --random --idle sw_fire
cougar-rgb set --delay 250                       # light delay for the current output, ms
```

Settings are stored in `~/.config/cougar-rgb/config.json`. Hardware effects set without the
daemon are applied directly; software and music effects need the daemon.

## Development

```sh
pip install -e ".[dev,gui]"
ruff check .
pytest
```

The tests check protocol packets byte-for-byte through a patched `ioctl`, so they need
neither the controller nor an audio server.

## Credits

Protocol details and effect timings come from [OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB)'s
Gigabyte RGB Fusion 2 USB driver.

## License

[GPL-3.0-or-later](LICENSE)
