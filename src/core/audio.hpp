#pragma once

// Analysis of the audio playing on the system: captures the default sink monitor through libpulse.
// When the default output device changes, capture moves to the new sink.

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace cougar {

inline constexpr int AUDIO_RATE = 48000;
inline constexpr int AUDIO_WINDOW = 2048;   // FFT window (~43 ms), ~23 Hz resolution, enough for bass
inline constexpr int AUDIO_HOP = 512;       // a new analysis every ~11 ms
inline constexpr int AUDIO_FRAGMENT = 2 * AUDIO_HOP;  // audio is fetched in ~21 ms chunks: half the wakeups
inline constexpr double MAX_DELAY = 1.0;    // maximum light delay, s

struct Features {
    double bass = 0, mid = 0, treble = 0;   // 0..1, normalized band loudness
    double level = 0;                       // overall loudness 0..1
    long beats = 0;                         // bass beat counter
    double time = 0;                        // when computed (clock::now)
    bool active = false;                    // sound above the silence threshold
};

// Pure signal processing: window, FFT, bands, auto gain, beat detection.
class SpectrumAnalyzer {
public:
    SpectrumAnalyzer();
    void reset();
    Features feed(std::span<const float> hop, double now);

private:
    std::vector<float> buffer_;
    std::vector<float> window_;
    std::vector<float> re_, im_;                        // FFT work buffers
    std::vector<float> twiddle_re_, twiddle_im_;
    std::vector<uint32_t> bitrev_;
    std::array<std::pair<int, int>, 3> band_bins_{};    // [first, last) bin of each band
    std::array<double, 3> peaks_{};
    double peak_level_ = 1e-3;
    double bass_avg_ = 0;
    long beats_ = 0;
    double last_beat_ = -1e9;
};

class Analyzer {
public:
    Analyzer() = default;
    ~Analyzer();
    Analyzer(const Analyzer &) = delete;
    Analyzer &operator=(const Analyzer &) = delete;

    void start();
    void stop();
    bool running() const { return running_; }

    // Light delay per output device, ms.
    void set_delays(std::map<std::string, int> delays);
    double delay() const;                   // for the current output, s
    std::string sink() const;

    Features latest() const;
    Features current(double now) const;     // with the light delay applied

    // For the capture and tests.
    void push(const Features &features);
    void set_sink(const std::string &sink);
    void set_capture_enabled(bool enabled) { capture_enabled_ = enabled; }

private:
    friend struct PulseSession;
    void run();

    mutable std::mutex mutex_;
    std::deque<Features> history_;
    Features latest_;
    std::map<std::string, int> delays_;
    std::string sink_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    bool capture_enabled_ = true;
    std::thread thread_;
    std::mutex wait_mutex_;
    std::condition_variable wait_cv_;
};

Analyzer &analyzer();

}  // namespace cougar
