#pragma once

// Низкоуровневый протокол Gigabyte RGB Fusion 2 USB (ITE 048d:5702, прошивка IT5701).
//
// Все команды — HID feature-репорты 0xCC длиной 64 байта.
// Корпус висит на разъёме D_LED1 (Z790 GAMING X AX): эффекты — зона 5,
// попиксельные данные — регистр 0x58 и бит 0x01 команды 0x32.
// Прошивка не переставляет байты цвета, лента ждёт их в порядке GRB — и в эффектах, и в кадрах.

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

inline constexpr int EFFECT_ZONE = 5;       // зона встроенных эффектов для D_LED1
inline constexpr uint8_t DIRECT_BIT = 0x01; // бит команды 0x32: разъём в попиксельном режиме
inline constexpr uint8_t DIRECT_REG = 0x58; // регистр попиксельных данных
inline constexpr int LED_COUNT = 8;         // светодиодов на вентилятор (хаб дублирует их на все вентиляторы)
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

// Параметры встроенного эффекта прошивки.
struct EffectPacket {
    uint8_t type = 1;                          // 1 static, 2 pulse, 3 flash, 4 color cycle
    Rgb8 color{};
    uint8_t max_brightness = 255;
    uint8_t min_brightness = 0;
    Rgb8 color1{};
    std::array<uint16_t, 4> periods{};         // мс: нарастание, затухание, удержание, ...
    std::array<uint8_t, 4> params{};
};

// Канал обмена feature-репортами; подменяется в тестах.
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

// Высокоуровневые команды, которыми пользуется движок; подменяется в тестах.
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
    void init() override;                   // без аудио-режима, короткая лента на ARGB-разъёмах
    void set_direct(bool enabled) override;
    void set_effect(int zone, const EffectPacket &effect) override;
    void apply() override;
    void write_leds(std::span<const Rgb8> colors) override;

private:
    std::unique_ptr<Transport> transport_;
    std::string path_;
};

}  // namespace cougar
