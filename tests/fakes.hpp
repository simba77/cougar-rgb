#pragma once

#include "audio.hpp"
#include "clock.hpp"
#include "device.hpp"

#include <string>
#include <vector>

namespace cougar::test {

// Записывает пакеты вместо отправки на контроллер.
struct RecordingTransport : Transport {
    std::vector<Packet> sent;
    Packet reply{};
    void set_feature(const Packet &packet) override { sent.push_back(packet); }
    Packet get_feature(uint8_t) override { return reply; }
};

// Записывает высокоуровневые вызовы движка.
struct FakeController : Controller {
    struct Call {
        std::string name;
        int zone = -1;
        bool flag = false;
        EffectPacket effect;
    };
    std::vector<Call> calls;

    void init() override { calls.push_back({"init"}); }
    void set_direct(bool enabled) override { calls.push_back({"set_direct", -1, enabled}); }
    void set_effect(int zone, const EffectPacket &e) override { calls.push_back({"set_effect", zone, false, e}); }
    void apply() override { calls.push_back({"apply"}); }
    void write_leds(std::span<const Rgb8>) override { calls.push_back({"write_leds"}); }
    DeviceInfo info() override { return {}; }

    std::vector<std::string> names() const
    {
        std::vector<std::string> out;
        for (const auto &c : calls)
            out.push_back(c.name);
        return out;
    }
};

// Управляемые часы и анализатор без реального захвата звука.
struct TestClock {
    double now = 1000.0;
    TestClock()
    {
        clock::set_for_tests([this] { return now; });
        analyzer().stop();
        analyzer().set_capture_enabled(false);
        analyzer().set_delays({});
        analyzer().set_sink("");
    }
    ~TestClock()
    {
        analyzer().stop();
        clock::set_for_tests(nullptr);
    }
};

}  // namespace cougar::test
