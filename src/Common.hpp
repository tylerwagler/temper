#pragma once

#include <string>
#include <vector>

namespace temper {

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

} // namespace temper
