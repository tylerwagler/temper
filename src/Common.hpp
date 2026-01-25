#pragma once

#include <string>
#include <vector>

namespace nvml_tool {

// TemperatureUnit enum removed - standardized on Celsius

struct CurvePoint {
    unsigned int temp;
    unsigned int value;

    bool operator<(const CurvePoint& other) const {
        return temp < other.temp;
    }
};

static constexpr int MAX_DEVICES = 64;
static constexpr int MAX_NAME_LEN = 256;
static constexpr int MAX_SETPOINTS = 16;

} // namespace nvml_tool
