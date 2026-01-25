#include "IpmiController.hpp"
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace nvml_tool {

IpmiController::IpmiController() {
    const char* h = std::getenv("IDRAC_IP");
    const char* u = std::getenv("IDRAC_USER");
    const char* p = std::getenv("IDRAC_PASS");
    
    if (h) host_ = h;
    if (u) user_ = u;
    if (p) pass_ = p;
    
    if (isEnabled()) {
        std::cout << "IPMI Controller initialized for host: " << host_ << std::endl;
        // Disable third-party fan control on Dell servers to allow manual control
        // raw 0x30 0x30 0x01 0x00
        executeRaw("0x30 0x30 0x01 0x00");
    }
}

void IpmiController::setChassisFanSpeed(unsigned int speedPercent) {
    if (!isEnabled()) return;
    
    // Command: raw 0x30 0x30 0x02 0xff <hex_speed>
    // Convert 0-100 to 0x00-0x64 (which is 100 in hex)
    std::stringstream ss;
    ss << "0x30 0x30 0x02 0xff 0x" << std::hex << speedPercent;
    executeRaw(ss.str());
}

void IpmiController::executeRaw(const std::string& rawCommand) {
    if (host_.empty()) return;
    
    // -N 1: 1 second timeout for LAN interface
    // -R 1: 1 retry
    std::string cmd = "ipmitool -I lanplus -H " + host_ + 
                      " -U " + user_ + 
                      " -P " + pass_ + 
                      " -N 1 -R 1" +
                      " raw " + rawCommand + " > /dev/null 2>&1";
    
    int res = std::system(cmd.c_str());
    if (res != 0) {
        // We don't want to spam if it fails, but let's log it once
        static bool failed = false;
        if (!failed) {
            std::cerr << "IPMI Command failed (check credentials/network): " << rawCommand << std::endl;
            failed = true;
        }
    }
}

} // namespace nvml_tool
