// Background daemon: keeps the effect applied and renders software animations.

#include "cli.hpp"

#include "audio.hpp"
#include "clock.hpp"
#include "config.hpp"
#include "device.hpp"
#include "engine.hpp"
#include "paths.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <memory>
#include <optional>
#include <sys/file.h>
#include <sys/stat.h>
#include <systemd/sd-bus.h>
#include <thread>
#include <unistd.h>

namespace cougar::cli {

namespace {

constexpr double RECONNECT_DELAY = 2.0;
constexpr double HEALTH_INTERVAL = 5.0;
constexpr double RESUME_SETTLE = 2.0;

std::atomic<bool> g_running{true};

void log(const char *level, const char *fmt, ...)
{
    std::va_list args;
    va_start(args, fmt);
    std::fprintf(stderr, "%s ", level);
    std::vfprintf(stderr, fmt, args);
    std::fputc('\n', stderr);
    va_end(args);
}

void sleep_for(double seconds)
{
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

double suspend_gap()
{
    timespec boot{}, mono{};
    ::clock_gettime(CLOCK_BOOTTIME, &boot);
    ::clock_gettime(CLOCK_MONOTONIC, &mono);
    return double(boot.tv_sec - mono.tv_sec) + double(boot.tv_nsec - mono.tv_nsec) / 1e9;
}

std::optional<std::timespec> config_stamp()
{
    struct stat st{};
    if (::stat(paths::config_file().c_str(), &st) != 0)
        return std::nullopt;
    return st.st_mtim;
}

bool same_stamp(const std::optional<std::timespec> &a, const std::optional<std::timespec> &b)
{
    if (!a || !b)
        return !a && !b;
    return a->tv_sec == b->tv_sec && a->tv_nsec == b->tv_nsec;
}

// Detects resume from logind's PrepareForSleep(false) signal.
//
// The CLOCK_BOOTTIME/CLOCK_MONOTONIC gap is not reliable on its own:
// with a wrong hardware clock the kernel barely accounts for the time spent asleep.
class SleepWatcher {
public:
    std::atomic<bool> resumed{false};

    void start() { thread_ = std::thread([this] { run(); }); }

    void stop()
    {
        stop_ = true;
        if (thread_.joinable())
            thread_.join();
    }

private:
    static int on_signal(sd_bus_message *m, void *userdata, sd_bus_error *)
    {
        int start = 0;
        if (sd_bus_message_read(m, "b", &start) >= 0 && !start)
            static_cast<SleepWatcher *>(userdata)->resumed = true;
        return 0;
    }

    void run()
    {
        while (!stop_) {
            sd_bus *bus = nullptr;
            if (sd_bus_open_system(&bus) >= 0 &&
                sd_bus_match_signal(bus, nullptr, "org.freedesktop.login1", "/org/freedesktop/login1",
                                    "org.freedesktop.login1.Manager", "PrepareForSleep", on_signal, this) >= 0) {
                while (!stop_) {
                    const int r = sd_bus_process(bus, nullptr);
                    if (r < 0)
                        break;
                    if (r == 0 && sd_bus_wait(bus, 500'000) < 0)
                        break;
                }
            } else {
                log("WARNING", "cannot watch logind sleep signals");
            }
            sd_bus_flush_close_unref(bus);
            for (int i = 0; i < 50 && !stop_; ++i)
                sleep_for(0.1);
        }
    }

    std::atomic<bool> stop_{false};
    std::thread thread_;
};

class Daemon {
public:
    void run()
    {
        sleep_watcher_.start();
        while (g_running) {
            try {
                step();
            } catch (const DeviceError &e) {
                log("WARNING", "device error: %s, reconnecting", e.what());
                disconnect();
                sleep_for(RECONNECT_DELAY);
            }
        }
        disconnect();
        sleep_watcher_.stop();
        analyzer().stop();
    }

private:
    void disconnect()
    {
        engine_.reset();
        dev_.reset();
    }

    bool connect()
    {
        try {
            dev_ = Fusion2::open();
            engine_ = std::make_unique<Engine>(*dev_);
            engine_->setup();
            log("INFO", "connected to %s (%s)", dev_->path().c_str(), dev_->info().name.c_str());
            force_apply_ = true;
            return true;
        } catch (const DeviceNotFound &) {
        } catch (const DeviceError &e) {
            log("WARNING", "connect failed: %s", e.what());
        }
        disconnect();
        return false;
    }

    void apply_config()
    {
        const Config cfg = Config::load(paths::config_file());
        engine_->apply(cfg.effect, cfg.params(), cfg.audio_delays);
        log("INFO", "applied %s, brightness %d%%", cfg.effect.c_str(), cfg.brightness);
    }

    void step()
    {
        double gap = suspend_gap();
        if (sleep_watcher_.resumed || gap - gap_ > 1.0) {
            log("INFO", "resume from suspend detected, reinitializing controller");
            sleep_for(RESUME_SETTLE);
            sleep_watcher_.resumed = false;
            disconnect();
            gap = suspend_gap();
        }
        gap_ = gap;

        if (!dev_ && !connect()) {
            sleep_for(RECONNECT_DELAY);
            return;
        }

        const auto stamp = config_stamp();
        if (force_apply_ || !same_stamp(stamp, stamp_)) {
            force_apply_ = false;
            stamp_ = stamp;
            apply_config();
        }

        const double now = clock::now();
        if (engine_->animated()) {
            engine_->tick();
            next_frame_ = std::max(next_frame_ + 1.0 / engine_->fps(), now);
            sleep_for(next_frame_ - clock::now());
        } else {
            if (now - last_health_ > HEALTH_INTERVAL) {
                last_health_ = now;
                dev_->info();       // throws DeviceError if the controller is gone
            }
            sleep_for(0.1);
        }
    }

    std::unique_ptr<Fusion2> dev_;
    std::unique_ptr<Engine> engine_;
    SleepWatcher sleep_watcher_;
    std::optional<std::timespec> stamp_;
    bool force_apply_ = true;
    double gap_ = suspend_gap();
    double last_health_ = 0;
    double next_frame_ = 0;
};

}  // namespace

int run_daemon()
{
    const auto lock_path = paths::lock_file();
    const int lock = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lock < 0 || ::flock(lock, LOCK_EX | LOCK_NB) != 0) {
        std::fprintf(stderr, "cougar-rgb daemon is already running\n");
        return 1;
    }
    struct sigaction sa{};
    sa.sa_handler = [](int) { g_running = false; };
    ::sigaction(SIGTERM, &sa, nullptr);
    ::sigaction(SIGINT, &sa, nullptr);

    Daemon().run();
    ::close(lock);
    return 0;
}

}  // namespace cougar::cli
