#include "audio.hpp"

#include "clock.hpp"

#include <bit>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <pulse/pulseaudio.h>

namespace cougar {

namespace {

constexpr double SILENCE_RMS = 1e-4;
const double PEAK_DECAY = std::pow(0.5, double(AUDIO_HOP) / AUDIO_RATE / 4.0);  // peak half-life ~4 s
constexpr double BEAT_AVG = double(AUDIO_HOP) / AUDIO_RATE / 0.25;              // bass average over ~0.25 s
constexpr double BEAT_RATIO = 1.35;
constexpr double BEAT_MIN_LEVEL = 0.3;
constexpr double BEAT_COOLDOWN = 0.12;
constexpr size_t HISTORY = size_t((MAX_DELAY + 0.5) * AUDIO_RATE / AUDIO_HOP);
constexpr std::array<std::pair<double, double>, 3> BANDS{{{30, 150}, {150, 2000}, {2000, 10000}}};

uint32_t reverse_bits(uint32_t value, int bits)
{
    uint32_t out = 0;
    for (int i = 0; i < bits; ++i, value >>= 1)
        out = out << 1 | (value & 1);
    return out;
}

}  // namespace

SpectrumAnalyzer::SpectrumAnalyzer()
    : window_(AUDIO_WINDOW), re_(AUDIO_WINDOW), im_(AUDIO_WINDOW), twiddle_re_(AUDIO_WINDOW / 2),
      twiddle_im_(AUDIO_WINDOW / 2), bitrev_(AUDIO_WINDOW)
{
    // Everything independent of the signal is computed once: Hann window, twiddle factors,
    // bit-reversal permutation and band limits in bins.
    for (int i = 0; i < AUDIO_WINDOW; ++i)
        window_[i] = float(0.5 - 0.5 * std::cos(2 * std::numbers::pi * i / (AUDIO_WINDOW - 1)));
    for (int k = 0; k < AUDIO_WINDOW / 2; ++k) {
        twiddle_re_[k] = float(std::cos(-2 * std::numbers::pi * k / AUDIO_WINDOW));
        twiddle_im_[k] = float(std::sin(-2 * std::numbers::pi * k / AUDIO_WINDOW));
    }
    const int bits = std::countr_zero(unsigned(AUDIO_WINDOW));
    for (uint32_t i = 0; i < uint32_t(AUDIO_WINDOW); ++i)
        bitrev_[i] = reverse_bits(i, bits);
    for (size_t band = 0; band < BANDS.size(); ++band) {
        auto bin = [](double freq) { return int(std::ceil(freq * AUDIO_WINDOW / AUDIO_RATE)); };
        band_bins_[band] = {bin(BANDS[band].first), std::min(bin(BANDS[band].second), AUDIO_WINDOW / 2 + 1)};
    }
    reset();
}

void SpectrumAnalyzer::reset()
{
    buffer_.assign(AUDIO_WINDOW, 0.0f);
    peaks_.fill(1e-3);
    peak_level_ = 1e-3;
    bass_avg_ = 0;
    beats_ = 0;
    last_beat_ = -1e9;
}

Features SpectrumAnalyzer::feed(std::span<const float> hop, double now)
{
    std::move(buffer_.begin() + hop.size(), buffer_.end(), buffer_.begin());
    std::copy(hop.begin(), hop.end(), buffer_.end() - hop.size());

    double sum = 0;
    for (auto it = buffer_.end() - 2 * AUDIO_HOP; it != buffer_.end(); ++it)
        sum += double(*it) * *it;
    const double rms = std::sqrt(sum / (2 * AUDIO_HOP));
    if (rms < SILENCE_RMS)
        return Features{.beats = beats_, .time = now};

    // Iterative radix-2 FFT using the precomputed tables.
    // Explicit arithmetic instead of std::complex: without -ffast-math its multiply calls the slow __mulsc3.
    for (int i = 0; i < AUDIO_WINDOW; ++i) {
        re_[bitrev_[i]] = buffer_[i] * window_[i];
        im_[bitrev_[i]] = 0;
    }
    for (int len = 2; len <= AUDIO_WINDOW; len <<= 1) {
        const int half = len / 2, stride = AUDIO_WINDOW / len;
        for (int i = 0; i < AUDIO_WINDOW; i += len)
            for (int k = 0; k < half; ++k) {
                const float wr = twiddle_re_[k * stride], wi = twiddle_im_[k * stride];
                const int a = i + k, b = a + half;
                const float vr = re_[b] * wr - im_[b] * wi, vi = re_[b] * wi + im_[b] * wr;
                re_[b] = re_[a] - vr, im_[b] = im_[a] - vi;
                re_[a] += vr, im_[a] += vi;
            }
    }

    std::array<double, 3> values{};
    for (size_t band = 0; band < BANDS.size(); ++band) {
        double power = 0;
        for (int k = band_bins_[band].first; k < band_bins_[band].second; ++k)
            power += double(re_[k]) * re_[k] + double(im_[k]) * im_[k];
        const double amp = std::sqrt(power);
        peaks_[band] = std::max(amp, peaks_[band] * PEAK_DECAY);
        values[band] = amp / peaks_[band];
    }
    peak_level_ = std::max(rms, peak_level_ * PEAK_DECAY);

    const double bass = values[0];
    if (bass > bass_avg_ * BEAT_RATIO && bass > BEAT_MIN_LEVEL && now - last_beat_ > BEAT_COOLDOWN) {
        ++beats_;
        last_beat_ = now;
    }
    bass_avg_ += (bass - bass_avg_) * BEAT_AVG;

    return Features{.bass = bass, .mid = values[1], .treble = values[2], .level = rms / peak_level_,
                    .beats = beats_, .time = now, .active = true};
}

// --------------------------------------------------------------------------------------------
// PulseAudio/PipeWire session: connection, tracking of the default sink and capture of its monitor.

struct PulseSession {
    Analyzer &owner;
    pa_threaded_mainloop *loop = nullptr;
    pa_context *context = nullptr;
    pa_stream *stream = nullptr;
    std::string stream_sink;
    std::vector<float> pending;
    SpectrumAnalyzer dsp;
    bool failed = false;

    explicit PulseSession(Analyzer &a) : owner(a) {}

    void finish()
    {
        {
            std::lock_guard lock(owner.wait_mutex_);
            failed = true;
        }
        owner.wait_cv_.notify_all();
    }

    static void on_context_state(pa_context *c, void *userdata)
    {
        auto *self = static_cast<PulseSession *>(userdata);
        switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY:
            pa_context_set_subscribe_callback(c, on_event, self);
            pa_operation_unref(pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SERVER, nullptr, nullptr));
            pa_operation_unref(pa_context_get_server_info(c, on_server_info, self));
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            self->finish();
            break;
        default:
            break;
        }
    }

    static void on_event(pa_context *c, pa_subscription_event_type_t type, uint32_t, void *userdata)
    {
        if ((type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SERVER)
            pa_operation_unref(pa_context_get_server_info(c, on_server_info, userdata));
    }

    static void on_server_info(pa_context *, const pa_server_info *info, void *userdata)
    {
        auto *self = static_cast<PulseSession *>(userdata);
        if (!info || !info->default_sink_name)
            return;
        const std::string sink = info->default_sink_name;
        if (sink != self->stream_sink || !self->stream)
            self->connect_stream(sink);
    }

    void disconnect_stream()
    {
        if (!stream)
            return;
        pa_stream_set_read_callback(stream, nullptr, nullptr);
        if (pa_stream_get_state(stream) == PA_STREAM_CREATING) {
            // pa_stream_disconnect() is refused until the server confirms the stream, and an abandoned
            // stream keeps capturing: its unread data stalls the live stream. Finish it once it is ready;
            // the context holds its own reference until then.
            pa_stream_set_state_callback(stream, on_abandoned_state, nullptr);
        } else {
            pa_stream_set_state_callback(stream, nullptr, nullptr);
            pa_stream_disconnect(stream);
        }
        pa_stream_unref(stream);
        stream = nullptr;
    }

    static void on_abandoned_state(pa_stream *s, void *)
    {
        if (pa_stream_get_state(s) == PA_STREAM_READY)
            pa_stream_disconnect(s);
    }

    void connect_stream(const std::string &sink)
    {
        disconnect_stream();
        stream_sink = sink;
        owner.set_sink(sink);
        dsp.reset();
        pending.clear();

        const pa_sample_spec spec{PA_SAMPLE_FLOAT32LE, AUDIO_RATE, 1};
        stream = pa_stream_new(context, "cougar-rgb capture", &spec, nullptr);
        if (!stream)
            return;
        pa_stream_set_read_callback(stream, on_read, this);
        pa_stream_set_state_callback(stream, on_stream_state, this);
        pa_buffer_attr attr{};
        attr.maxlength = uint32_t(-1);
        attr.fragsize = AUDIO_FRAGMENT * sizeof(float);
        const std::string monitor = sink + ".monitor";
        if (pa_stream_connect_record(stream, monitor.c_str(), &attr, PA_STREAM_ADJUST_LATENCY) < 0)
            disconnect_stream();
    }

    static void on_stream_state(pa_stream *s, void *userdata)
    {
        auto *self = static_cast<PulseSession *>(userdata);
        const auto state = pa_stream_get_state(s);
        if (state == PA_STREAM_FAILED || state == PA_STREAM_TERMINATED)
            self->disconnect_stream();      // reconnect on the next server event
    }

    static void on_read(pa_stream *s, size_t, void *userdata)
    {
        auto *self = static_cast<PulseSession *>(userdata);
        const void *data = nullptr;
        size_t bytes = 0;
        while (pa_stream_readable_size(s) > 0) {
            if (pa_stream_peek(s, &data, &bytes) < 0)
                return;
            if (bytes == 0)
                break;
            if (data) {
                const auto *samples = static_cast<const float *>(data);
                self->pending.insert(self->pending.end(), samples, samples + bytes / sizeof(float));
            }
            pa_stream_drop(s);
        }
        size_t used = 0;
        while (self->pending.size() - used >= AUDIO_HOP) {
            const std::span<const float> hop(self->pending.data() + used, AUDIO_HOP);
            self->owner.push(self->dsp.feed(hop, clock::now()));
            used += AUDIO_HOP;
        }
        self->pending.erase(self->pending.begin(), self->pending.begin() + used);
    }

    // Runs while the session is alive and the analyzer is not stopped.
    void run()
    {
        loop = pa_threaded_mainloop_new();
        context = pa_context_new(pa_threaded_mainloop_get_api(loop), "cougar-rgb");
        pa_context_set_state_callback(context, on_context_state, this);
        if (pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0 ||
            pa_threaded_mainloop_start(loop) < 0) {
            pa_context_unref(context);
            pa_threaded_mainloop_free(loop);
            return;
        }
        {
            std::unique_lock lock(owner.wait_mutex_);
            owner.wait_cv_.wait(lock, [&] { return failed || owner.stop_; });
        }
        pa_threaded_mainloop_lock(loop);
        disconnect_stream();
        pa_context_disconnect(context);
        pa_context_unref(context);
        pa_threaded_mainloop_unlock(loop);
        pa_threaded_mainloop_stop(loop);
        pa_threaded_mainloop_free(loop);
    }
};

// --------------------------------------------------------------------------------------------

Analyzer::~Analyzer()
{
    stop();
}

void Analyzer::start()
{
    if (running_)
        return;
    stop_ = false;
    running_ = true;
    if (capture_enabled_)
        thread_ = std::thread([this] { run(); });
}

void Analyzer::stop()
{
    if (!running_)
        return;
    {
        std::lock_guard lock(wait_mutex_);
        stop_ = true;
    }
    wait_cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
    running_ = false;
    push(Features{});
}

void Analyzer::run()
{
    while (!stop_) {
        PulseSession(*this).run();
        std::unique_lock lock(wait_mutex_);
        wait_cv_.wait_for(lock, std::chrono::seconds(1), [&] { return bool(stop_); });  // the server went away, retry
    }
}

void Analyzer::set_delays(std::map<std::string, int> delays)
{
    std::lock_guard lock(mutex_);
    delays_ = std::move(delays);
}

double Analyzer::delay() const
{
    std::lock_guard lock(mutex_);
    const auto it = delays_.find(sink_);
    const double ms = it == delays_.end() ? 0 : it->second;
    return std::clamp(ms / 1000, 0.0, MAX_DELAY);
}

std::string Analyzer::sink() const
{
    std::lock_guard lock(mutex_);
    return sink_;
}

void Analyzer::set_sink(const std::string &sink)
{
    std::lock_guard lock(mutex_);
    sink_ = sink;
}

void Analyzer::push(const Features &features)
{
    std::lock_guard lock(mutex_);
    latest_ = features;
    history_.push_back(features);
    while (history_.size() > HISTORY)
        history_.pop_front();
}

Features Analyzer::latest() const
{
    std::lock_guard lock(mutex_);
    return latest_;
}

Features Analyzer::current(double now) const
{
    const double d = delay();
    std::lock_guard lock(mutex_);
    if (d <= 0)
        return latest_;
    const double moment = now - d;
    for (auto it = history_.rbegin(); it != history_.rend(); ++it)
        if (it->time <= moment)
            return *it;
    return Features{.time = moment};
}

Analyzer &analyzer()
{
    static Analyzer instance;
    return instance;
}

}  // namespace cougar
