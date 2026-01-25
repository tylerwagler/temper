#include "CurveController.hpp"
#include <sstream>
#include <algorithm>
#include <iostream>

namespace nvml_tool {

void CurveController::parseSetpoints(const std::string& setpointString) {
    points_.clear();
    std::stringstream ss(setpointString);
    std::string token;
    
    while (ss >> token) {
        auto colonPos = token.find(':');
        if (colonPos != std::string::npos) {
            try {
                unsigned int temp = std::stoul(token.substr(0, colonPos));
                unsigned int val = std::stoul(token.substr(colonPos + 1));
                points_.push_back({temp, val});
            } catch (...) {
                // Skip invalid tokens
            }
        }
    }
    
    std::sort(points_.begin(), points_.end());
}

unsigned int CurveController::interpolate(unsigned int currentTemp) const {
    if (points_.empty()) return 0;
    if (currentTemp <= points_.front().temp) return points_.front().value;
    if (currentTemp >= points_.back().temp) return points_.back().value;

    for (size_t i = 0; i < points_.size() - 1; ++i) {
        if (currentTemp >= points_[i].temp && currentTemp <= points_[i+1].temp) {
            double tempRange = points_[i+1].temp - points_[i].temp;
            double valRange = (double)points_[i+1].value - points_[i].value;
            double tempOffset = (double)currentTemp - points_[i].temp;
            
            return (unsigned int)(points_[i].value + (valRange * tempOffset / tempRange));
        }
    }
    return points_.front().value;
}

} // namespace nvml_tool
