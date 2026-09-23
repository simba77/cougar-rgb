#pragma once

// Low-level Gigabyte RGB Fusion 2 USB protocol (ITE 048d:5702, IT5701 firmware).
//
// Every command is a 64-byte HID feature report with ID 0xCC.
// The case is on the D_LED1 header (Z790 GAMING X AX): effects use zone 5,
// per-LED data goes to register 0x58 with bit 0x01 of command 0x32.
// The firmware does not reorder color bytes; the strip expects GRB both in effects and in frames.

#include "color.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

namespace cougar {

inline constexpr uint16_t VID = 0x048D, PID = 0x5702;
inline constexpr uint8_t REPORT_ID = 0xCC;
inline constexpr size_t PACKET_SIZE = 64;

inline constexpr int EFFECT_ZONE = 5;       // builtin effect zone for D_LED1
inline constexpr uint8_t DIRECT_BIT = 0x01; // command 0x32 bit: header in per-LED mode
inline constexpr uint8_t DIRECT_REG = 0x58; // per-LED data register
inline constexpr int LED_COUNT = 8;         // LEDs per fan (the hub mirrors them onto every fan)
inline constexpr int ZONE_COUNT = 8;

inline constexpr uint8_t CMD_EFFECT = 0x20;
inline constexpr uint8_t CMD_APPLY = 0x28;
inline constexpr uint8_t CMD_BEAT = 0x31;
inline constexpr uint8_t CMD_DIRECT = 0x32;
inline constexpr uint8_t CMD_LED_COUNT = 0x34;
inline constexpr uint8_t CMD_INFO = 0x60;

using Packet = std::array<uint8_t, PACKET_SIZE>;
using Frame = std::array<Rgb8, LED_COUNT>;

struct DeviceError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct DeviceNotFound : DeviceError {
    using DeviceError::DeviceError;
};

struct DeviceAccessDenied : DeviceError {
    using DeviceError::DeviceError;
};

struct DeviceInfo {
    int product = 0;
    int device_num = 0;
    std::string firmware;
    std::string name;
    std::string chip_id;
};

// Parameters of a firmware builtin effect.
struct EffectPacket {
    uint8_t type = 1;                          // 1 static, 2 pulse, 3 flash, 4 color cycle
    Rgb8 color{};
    uint8_t max_brightness = 255;
    uint8_t min_brightness = 0;
    Rgb8 color1{};
    std::array<uint16_t, 4> periods{};         // ms: fade in, fade out, hold, ...
    std::array<uint8_t, 4> params{};
};

// Feature report channel; replaced in tests.
class Transport {
public:
    virtual ~Transport() = default;
    virtual void set_feature(const Packet &packet) = 0;
    virtual Packet get_feature(uint8_t report_id) = 0;
};

class HidrawTransport : public Transport {
public:
    explicit HidrawTransport(const std::filesystem::path &path);
    ~HidrawTransport() override;
    HidrawTransport(const HidrawTransport &) = delete;
    HidrawTransport &operator=(const HidrawTransport &) = delete;

    void set_feature(const Packet &packet) override;
    Packet get_feature(uint8_t report_id) override;

private:
    int fd_ = -1;
};

std::filesystem::path find_hidraw(const std::filesystem::path &sysfs = "/sys/class/hidraw");

// High-level commands used by the engine; replaced in tests.
class Controller {
public:
    virtual ~Controller() = default;
    virtual void init() = 0;
    virtual void set_direct(bool enabled) = 0;
    virtual void set_effect(int zone, const EffectPacket &effect) = 0;
    virtual void apply() = 0;
    virtual void write_leds(std::span<const Rgb8> colors) = 0;
    virtual DeviceInfo info() = 0;
};

class Fusion2 : public Controller {
public:
    explicit Fusion2(std::unique_ptr<Transport> transport, std::string path = {});
    static std::unique_ptr<Fusion2> open();

    const std::string &path() const { return path_; }

    void command(std::initializer_list<uint8_t> args);
    DeviceInfo info() override;
    void init() override;                   // no audio mode, short strips on the ARGB headers
    void set_direct(bool enabled) override;
    void set_effect(int zone, const EffectPacket &effect) override;
    void apply() override;
    void write_leds(std::span<const Rgb8> colors) override;

private:
    std::unique_ptr<Transport> transport_;
    std::string path_;
};

}  // namespace cougar
