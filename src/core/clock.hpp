#pragma once

#include <functional>

namespace cougar::clock {

// Монотонное время в секундах; в тестах подменяется.
double now();
void set_for_tests(std::function<double()> source);

}  // namespace cougar::clock
