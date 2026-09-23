#pragma once

#include <optional>
#include <string>

namespace cougar {

struct SinkInfo {
    std::string name;
    std::string description;
};

// Текущее устройство вывода по умолчанию (синхронный запрос к PulseAudio/PipeWire, до ~2 с).
std::optional<SinkInfo> default_sink();

}  // namespace cougar
