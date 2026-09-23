#pragma once

#include <optional>
#include <string>

namespace cougar {

struct SinkInfo {
    std::string name;
    std::string description;
};

// Current default output device (synchronous PulseAudio/PipeWire query, up to ~2 s).
std::optional<SinkInfo> default_sink();

}  // namespace cougar
