#pragma once

#include "Common.hpp"
#include <vector>
#include <string>

namespace temper {

class CurveController {
public:
    CurveController() = default;
    
    void parseSetpoints(const std::string& setpointString);
    unsigned int interpolate(unsigned int currentTemp) const;
    
    bool isEmpty() const { return points_.empty(); }
    const std::vector<CurvePoint>& getPoints() const { return points_; }

private:
    std::vector<CurvePoint> points_;
};

} // namespace temper
