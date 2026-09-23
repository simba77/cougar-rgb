#include "device.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <linux/hidraw.h>
#include <sstream>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

namespace cougar {

namespace {

[[noreturn]] void throw_errno(const std::string &what)
{
    throw DeviceError(what + ": " + std::strerror(errno));
}

void put_grb(Packet &buf, size_t offset, Rgb8 c)
{
    buf[offset] = c.g;
    buf[offset + 1] = c.r;
    buf[offset + 2] = c.b;
}

void put_u16(Packet &buf, size_t offset, uint16_t v)
{
    buf[offset] = v & 0xFF;
    buf[offset + 1] = v >> 8;
}

void put_u32(Packet &buf, size_t offset, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        buf[offset + i] = (v >> (8 * i)) & 0xFF;
}

}  // namespace

HidrawTransport::HidrawTransport(const std::filesystem::path &path)
{
    fd_ = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0 && (errno == EACCES || errno == EPERM))
        throw DeviceAccessDenied("no access to " + path.string());
    if (fd_ < 0)
        throw_errno("cannot open " + path.string());
}

HidrawTransport::~HidrawTransport()
{
    if (fd_ >= 0)
        ::close(fd_);
}

void HidrawTransport::set_feature(const Packet &packet)
{
    Packet buf = packet;
    if (::ioctl(fd_, HIDIOCSFEATURE(PACKET_SIZE), buf.data()) < 0)
        throw_errno("HIDIOCSFEATURE");
}

Packet HidrawTransport::get_feature(uint8_t report_id)
{
    Packet buf{};
    buf[0] = report_id;
    if (::ioctl(fd_, HIDIOCGFEATURE(PACKET_SIZE), buf.data()) < 0)
        throw_errno("HIDIOCGFEATURE");
    return buf;
}

std::filesystem::path find_hidraw(const std::filesystem::path &sysfs)
{
    char tag[32];
    std::snprintf(tag, sizeof tag, "HID_ID=0003:%08X:%08X", VID, PID);
    std::vector<std::filesystem::path> entries;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(sysfs, ec))
        entries.push_back(entry.path());
    std::sort(entries.begin(), entries.end());
    for (const auto &entry : entries) {
        std::ifstream uevent(entry / "device" / "uevent");
        std::stringstream content;
        content << uevent.rdbuf();
        if (content.str().find(tag) != std::string::npos)
            return std::filesystem::path("/dev") / entry.filename();
    }
    throw DeviceNotFound("RGB Fusion 2 controller (048d:5702) not found");
}

Fusion2::Fusion2(std::unique_ptr<Transport> transport, std::string path)
    : transport_(std::move(transport)), path_(std::move(path))
{
}

std::unique_ptr<Fusion2> Fusion2::open()
{
    auto path = find_hidraw();
    return std::make_unique<Fusion2>(std::make_unique<HidrawTransport>(path), path.string());
}

void Fusion2::command(std::initializer_list<uint8_t> args)
{
    Packet buf{};
    buf[0] = REPORT_ID;
    std::copy(args.begin(), args.end(), buf.begin() + 1);
    transport_->set_feature(buf);
}

DeviceInfo Fusion2::info()
{
    command({CMD_INFO});
    const Packet buf = transport_->get_feature(REPORT_ID);
    DeviceInfo info;
    info.product = buf[1];
    info.device_num = buf[2];
    char text[32];
    std::snprintf(text, sizeof text, "%d.%d.%d.%d", buf[4], buf[5], buf[6], buf[7]);
    info.firmware = text;
    const auto name_begin = buf.begin() + 12, name_end = buf.begin() + 40;
    info.name.assign(name_begin, std::find(name_begin, name_end, 0));
    uint32_t chip = buf[56] | buf[57] << 8 | buf[58] << 16 | uint32_t(buf[59]) << 24;
    std::snprintf(text, sizeof text, "0x%08X", chip);
    info.chip_id = text;
    return info;
}

void Fusion2::init()
{
    command({CMD_BEAT, 0});
    command({CMD_LED_COUNT, 0x00, 0x00, 0x00});
}

void Fusion2::set_direct(bool enabled)
{
    command({CMD_DIRECT, enabled ? DIRECT_BIT : uint8_t(0)});
}

void Fusion2::set_effect(int zone, const EffectPacket &e)
{
    Packet buf{};
    buf[0] = REPORT_ID;
    buf[1] = CMD_EFFECT + zone;
    put_u32(buf, 2, 1u << zone);
    buf[11] = e.type;
    buf[12] = e.max_brightness;
    buf[13] = e.min_brightness;
    put_grb(buf, 14, e.color);
    put_grb(buf, 18, e.color1);
    for (int i = 0; i < 4; ++i)
        put_u16(buf, 22 + 2 * i, e.periods[i]);
    std::copy(e.params.begin(), e.params.end(), buf.begin() + 30);
    transport_->set_feature(buf);
}

void Fusion2::apply()
{
    command({CMD_APPLY, 0xFF});
}

void Fusion2::write_leds(std::span<const Rgb8> colors)
{
    uint16_t offset = 0;
    for (size_t start = 0; start < colors.size(); start += 19) {
        const auto chunk = colors.subspan(start, std::min<size_t>(19, colors.size() - start));
        Packet buf{};
        buf[0] = REPORT_ID;
        buf[1] = DIRECT_REG;
        put_u16(buf, 2, offset);
        buf[4] = chunk.size() * 3;
        for (size_t i = 0; i < chunk.size(); ++i)
            put_grb(buf, 5 + i * 3, chunk[i]);
        transport_->set_feature(buf);
        offset += chunk.size() * 3;
    }
}

}  // namespace cougar
