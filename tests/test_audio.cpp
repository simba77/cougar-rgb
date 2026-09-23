#include "audio.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <vector>

using namespace cougar;

namespace {

std::vector<float> tone(double freq, double amplitude, int samples, int offset = 0)
{
    std::vector<float> out(samples);
    for (int i = 0; i < samples; ++i)
        out[i] = float(amplitude * std::sin(2 * std::numbers::pi * freq * (offset + i) / AUDIO_RATE));
    return out;
}

Features feed_all(SpectrumAnalyzer &dsp, const std::vector<float> &signal, double &t)
{
    Features last;
    for (size_t i = 0; i + AUDIO_HOP <= signal.size(); i += AUDIO_HOP) {
        t += double(AUDIO_HOP) / AUDIO_RATE;
        last = dsp.feed(std::span(signal).subspan(i, AUDIO_HOP), t);
    }
    return last;
}

}  // namespace

TEST_CASE("silence is reported as inactive")
{
    SpectrumAnalyzer dsp;
    double t = 0;
    const Features f = feed_all(dsp, std::vector<float>(AUDIO_RATE / 2), t);
    CHECK_FALSE(f.active);
    CHECK(f.level == 0);
}

TEST_CASE("a bass tone lands in the bass band")
{
    SpectrumAnalyzer dsp;
    double t = 0;
    // a broadband signal first, so auto gain remembers the peaks of all bands
    std::vector<float> mixed = tone(60, 0.3, AUDIO_RATE);
    const auto mid = tone(800, 0.3, AUDIO_RATE), high = tone(5000, 0.3, AUDIO_RATE);
    for (size_t i = 0; i < mixed.size(); ++i)
        mixed[i] += mid[i] + high[i];
    feed_all(dsp, mixed, t);

    const Features f = feed_all(dsp, tone(60, 0.3, AUDIO_RATE / 2), t);
    CHECK(f.active);
    CHECK(f.bass > 0.8);
    CHECK(f.mid < 0.2);
    CHECK(f.treble < 0.2);
}

TEST_CASE("bass hits are counted as beats")
{
    SpectrumAnalyzer dsp;
    double t = 0;
    std::vector<float> signal;
    for (int beat = 0; beat < 8; ++beat) {            // 8 hits every 0.5 s: 100 ms of kick drum, then a quiet background
        auto hit = tone(55, 0.8, AUDIO_RATE / 10);
        auto rest = tone(3000, 0.02, AUDIO_RATE * 4 / 10);
        signal.insert(signal.end(), hit.begin(), hit.end());
        signal.insert(signal.end(), rest.begin(), rest.end());
    }
    const Features f = feed_all(dsp, signal, t);
    CHECK(f.beats >= 7);
    CHECK(f.beats <= 8);
}
