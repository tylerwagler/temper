#pragma once

#include <string>
#include <vector>

namespace temper {

class IpmiController {
public:
    IpmiController();
    
    void setChassisFanSpeed(unsigned int speedPercent);
    bool isEnabled() const { return !host_.empty(); }

private:
    std::string host_;
    std::string user_;
    std::string pass_;
    
    void executeRaw(const std::string& rawCommand);
};

} // namespace temper
