#pragma once

// Анализ звука, который играет в системе: захват монитора выхода по умолчанию через libpulse.
// При смене устройства вывода по умолчанию захват переезжает на новый выход.

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
inline constexpr int AUDIO_WINDOW = 2048;   // окно FFT (~43 мс) — разрешение ~23 Гц, хватает для баса
inline constexpr int AUDIO_HOP = 512;       // новый анализ каждые ~11 мс
inline constexpr int AUDIO_FRAGMENT = 2 * AUDIO_HOP;  // звук забираем по ~21 мс: вдвое меньше пробуждений
inline constexpr double MAX_DELAY = 1.0;    // максимальная задержка света, с

struct Features {
    double bass = 0, mid = 0, treble = 0;   // 0..1, нормированная громкость полос
    double level = 0;                       // общая громкость 0..1
    long beats = 0;                         // счётчик ударов баса
    double time = 0;                        // когда посчитано (clock::now)
    bool active = false;                    // есть звук выше порога тишины
};

// Чистая обработка сигнала: окно, FFT, полосы, автоусиление, детектор ударов.
class SpectrumAnalyzer {
public:
    SpectrumAnalyzer();
    void reset();
    Features feed(std::span<const float> hop, double now);

private:
    std::vector<float> buffer_;
    std::vector<float> window_;
    std::vector<float> re_, im_;                        // рабочие буферы БПФ
    std::vector<float> twiddle_re_, twiddle_im_;
    std::vector<uint32_t> bitrev_;
    std::array<std::pair<int, int>, 3> band_bins_{};    // [первый, последний) бин каждой полосы
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

    // Задержка света по устройствам вывода, мс.
    void set_delays(std::map<std::string, int> delays);
    double delay() const;                   // для текущего выхода, с
    std::string sink() const;

    Features latest() const;
    Features current(double now) const;     // с учётом задержки света

    // Для захвата и тестов.
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
