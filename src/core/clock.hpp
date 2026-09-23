#pragma once

#include <functional>

namespace cougar::clock {

// Monotonic time in seconds; replaced in tests.
double now();
void set_for_tests(std::function<double()> source);

}  // namespace cougar::clock
