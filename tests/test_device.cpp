#include "device.hpp"
#include "fakes.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

using namespace cougar;
using test::RecordingTransport;

namespace {

struct Setup {
    RecordingTransport *transport;
    Fusion2 dev;
    Setup() : Setup(std::make_unique<RecordingTransport>()) {}
    explicit Setup(std::unique_ptr<RecordingTransport> t) : transport(t.get()), dev(std::move(t)) {}
};

uint16_t u16(const Packet &p, size_t at) { return p[at] | p[at + 1] << 8; }
uint32_t u32(const Packet &p, size_t at) { return p[at] | p[at + 1] << 8 | p[at + 2] << 16 | uint32_t(p[at + 3]) << 24; }

}  // namespace

TEST_CASE("command is a zero-padded feature report")
{
    Setup s;
    s.dev.command({CMD_APPLY, 0xFF});
    Packet expected{};
    expected[0] = 0xCC, expected[1] = 0x28, expected[2] = 0xFF;
    REQUIRE(s.transport->sent == std::vector{expected});
}

TEST_CASE("set_direct toggles the D_LED1 bit")
{
    Setup s;
    s.dev.set_direct(true);
    s.dev.set_direct(false);
    REQUIRE(s.transport->sent[0][1] == 0x32);
    REQUIRE(s.transport->sent[0][2] == DIRECT_BIT);
    REQUIRE(s.transport->sent[1][2] == 0);
}

TEST_CASE("effect packet layout")
{
    Setup s;
    s.dev.set_effect(EFFECT_ZONE, {.type = 2, .color = {255, 16, 1}, .max_brightness = 200, .min_brightness = 3,
                                   .periods = {1000, 900, 200, 0}, .params = {7, 1, 2, 0}});
    const Packet &buf = s.transport->sent.at(0);
    CHECK(buf[1] == 0x20 + EFFECT_ZONE);
    CHECK(u32(buf, 2) == 1u << EFFECT_ZONE);
    CHECK(u32(buf, 6) == 0);
    CHECK((buf[11] == 2 && buf[12] == 200 && buf[13] == 3));
    CHECK((buf[14] == 16 && buf[15] == 255 && buf[16] == 1 && buf[17] == 0));   // GRB + padding
    CHECK(u32(buf, 18) == 0);
    CHECK((u16(buf, 22) == 1000 && u16(buf, 24) == 900 && u16(buf, 26) == 200 && u16(buf, 28) == 0));
    CHECK((buf[30] == 7 && buf[31] == 1 && buf[32] == 2 && buf[33] == 0));
}

TEST_CASE("write_leds splits into 19-LED packets in GRB order")
{
    Setup s;
    std::vector<Rgb8> colors;
    for (int i = 0; i < 25; ++i)
        colors.push_back({uint8_t(i), uint8_t(100 + i), 200});
    s.dev.write_leds(colors);
    REQUIRE(s.transport->sent.size() == 2);
    const auto &first = s.transport->sent[0], &second = s.transport->sent[1];
    CHECK(first[1] == DIRECT_REG);
    CHECK((u16(first, 2) == 0 && first[4] == 57));
    CHECK((u16(second, 2) == 57 && second[4] == 18));
    CHECK((first[5] == 100 && first[6] == 0 && first[7] == 200));
    CHECK((second[5] == 119 && second[6] == 19 && second[7] == 200));
}

TEST_CASE("info parses the 0x60 report")
{
    auto transport = std::make_unique<RecordingTransport>();
    Packet &r = transport->reply;
    r[0] = 0xCC, r[1] = 1, r[4] = 3, r[5] = 5, r[6] = 5, r[7] = 0;
    const std::string name = "IT5701-GIGABYTE V3.5.5.0";
    std::copy(name.begin(), name.end(), r.begin() + 12);
    r[56] = 0x00, r[57] = 0x01, r[58] = 0x01, r[59] = 0x57;
    Setup s(std::move(transport));
    const DeviceInfo info = s.dev.info();
    CHECK(s.transport->sent.at(0)[1] == CMD_INFO);
    CHECK(info.firmware == "3.5.5.0");
    CHECK(info.name == name);
    CHECK(info.chip_id == "0x57010100");
}

TEST_CASE("find_hidraw matches vendor and product")
{
    const auto root = std::filesystem::temp_directory_path() / "cougar-rgb-test-sysfs";
    std::filesystem::remove_all(root);
    for (auto [name, id] : {std::pair{"hidraw0", "0003:000009DA:00009090"}, {"hidraw2", "0003:0000048D:00005702"}}) {
        std::filesystem::create_directories(root / name / "device");
        std::ofstream(root / name / "device" / "uevent") << "DRIVER=hid-generic\nHID_ID=" << id << "\n";
    }
    CHECK(find_hidraw(root) == "/dev/hidraw2");
    std::filesystem::remove_all(root / "hidraw2");
    CHECK_THROWS_AS(find_hidraw(root), DeviceNotFound);
    std::filesystem::remove_all(root);
}
