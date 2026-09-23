#include "clock.hpp"

#include <chrono>

namespace cougar::clock {

namespace {
std::function<double()> &source()
{
    static std::function<double()> fn;
    return fn;
}
}  // namespace

double now()
{
    if (source())
        return source()();
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void set_for_tests(std::function<double()> fn)
{
    source() = std::move(fn);
}

}  // namespace cougar::clock
