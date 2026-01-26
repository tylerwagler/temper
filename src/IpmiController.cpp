#include "IpmiController.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <array>
#include <memory>
#include <algorithm>
#include <thread>
#include <mutex>
#include <string>
#include <array>
#include <cstdio> // For popen/pclose

namespace temper {

IpmiController::IpmiController() : polling_(false) {
    metrics_.inletTemp = 0;
    metrics_.exhaustTemp = 0;
    metrics_.powerConsumption = 0;
    metrics_.available = false;
}
IpmiController::~IpmiController() {}

void IpmiController::init(const std::string& host, const std::string& user, const std::string& pass) {
    host_ = host;
    user_ = user;
    pass_ = pass;
    std::cout << "IPMI Controller initialized for host: " << host_ << std::endl;
}

void IpmiController::setChassisFanSpeed(unsigned int speedPercent) {
    if (host_.empty()) return;

    // Check if we need to take manual control (0x30 0x30 0x01 0x00)
    executeRaw("0x30 0x30 0x01 0x00"); // Enable Manual Control

    // Set fan speeds
    std::stringstream ss;
    ss << "0x30 0x30 0x02 0xff"; // All fans
    ss << " " << "0x" << std::hex << speedPercent;
    
    executeRaw(ss.str());
}

// Helper to execute command and get output
std::string execCommand(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

void IpmiController::pollMetrics() {
    if (host_.empty() || polling_.exchange(true)) return;

    struct Guard {
        std::atomic<bool>& flag;
        ~Guard() { flag.store(false); }
    } guard{polling_};

    std::string cmd;
    if (useSsh_) {
        // Use racadm via SSH (more stable)
        cmd = "sshpass -p '" + pass_ + "' ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5 " + 
              user_ + "@" + host_ + " \"racadm getsensorinfo\"";
    } else {
        cmd = "ipmitool -I lanplus -H " + host_ + " -U " + user_ + " -P '" + pass_ + "' -N 2 -R 1 sdr list";
    }

    std::string output = execCommand(cmd.c_str());
    
    IpmiMetrics m;
    m.available = !output.empty();
    m.inletTemp = 0;
    m.exhaustTemp = 0;
    m.powerConsumption = 0;

    auto extractFirstNumber = [](const std::string& s) -> int {
        size_t first = s.find_first_of("0123456789");
        if (first == std::string::npos) return -1;
        try {
            return std::stoi(s.substr(first));
        } catch (...) { return -1; }
    };

    auto extractValueAfter = [&](const std::string& line, const std::string& keyword) -> int {
        size_t pos = line.find(keyword);
        if (pos == std::string::npos) return -1;
        // Skip the keyword and look for the next numeric value
        return extractFirstNumber(line.substr(pos + keyword.length()));
    };

    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (useSsh_) {
            // racadm format: "[Sensor Name] [Status] [Reading] [Units]"
            if (line.find("Inlet Temp") != std::string::npos && line.find("[Key") == std::string::npos) {
                m.inletTemp = extractValueAfter(line, "Ok");
                if (m.inletTemp > 0) m.available = true;
            } else if (line.find("Exhaust Temp") != std::string::npos && line.find("[Key") == std::string::npos) {
                m.exhaustTemp = extractValueAfter(line, "Ok");
            } else if ((line.find("Pwr Consumption") != std::string::npos || line.find("System Level") != std::string::npos) && line.find("[Key") == std::string::npos) {
                 int p = extractValueAfter(line, "Ok");
                 if (p > 0) m.powerConsumption = p;
            } else if (line.find("CPU") != std::string::npos && line.find("Temp") != std::string::npos && line.find("[Key") == std::string::npos) {
                int t = extractValueAfter(line, "Ok");
                if (t > 1) m.cpuTemps.push_back((unsigned int)t); 
            } else if (line.find("Fan") != std::string::npos && line.find("RPM") != std::string::npos && line.find("[Key") == std::string::npos) {
                int rpm = extractValueAfter(line, "Ok");
                if (rpm > 100) m.fanSpeeds.push_back((unsigned int)rpm);
            }
        } else {
            // ipmitool SDR format
            size_t firstPipe = line.find('|');
            if (firstPipe == std::string::npos) continue;
            
            std::string name = line.substr(0, firstPipe);
            name.erase(name.find_last_not_of(" \n\r\t")+1);

            size_t secondPipe = line.find('|', firstPipe + 1);
            if (secondPipe == std::string::npos) continue;

            std::string valueStr = line.substr(firstPipe + 1, secondPipe - firstPipe - 1);
            int val = extractFirstNumber(valueStr);
            if (val == -1) continue;

            if (name == "Inlet Temp") {
                m.inletTemp = (unsigned int)val; 
                m.available = true;
            } else if (name == "Exhaust Temp") {
                m.exhaustTemp = (unsigned int)val;
            } else if (name == "Pwr Consumption" || name.find("Watt") != std::string::npos) {
                m.powerConsumption = (unsigned int)val;
            } else if (name == "Temp" || (name.find("Temp") != std::string::npos && name.find("Inlet") == std::string::npos && name.find("Exhaust") == std::string::npos)) {
                m.cpuTemps.push_back((unsigned int)val);
            } else if (name.find("Fan") == 0 && name.find("Redundancy") == std::string::npos) {
                if (valueStr.find("RPM") != std::string::npos) {
                    m.fanSpeeds.push_back((unsigned int)val);
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(metricsMutex_);
        metrics_ = m;
    }

    if (m.available) {
        std::cout << "[IPMI] Poll successful. Inlet: " << m.inletTemp << "C, Fans: " << m.fanSpeeds.size() << ", CPUs: " << m.cpuTemps.size() << std::endl;
    } else {
        std::cerr << "[IPMI] Poll failed or empty output." << std::endl;
    }
}

IpmiMetrics IpmiController::getMetrics() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return metrics_;
}

void IpmiController::executeRaw(const std::string& rawCommand) {
    if (host_.empty() || polling_.exchange(true)) return;
    
    struct Guard {
        std::atomic<bool>& flag;
        ~Guard() { flag.store(false); }
    } guard{polling_};

    std::string cmd;
    if (useSsh_) {
        cmd = "sshpass -p '" + pass_ + "' ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5 " + 
              user_ + "@" + host_ + " \"raw " + rawCommand + "\" > /dev/null 2>&1";
    } else {
        cmd = "ipmitool -I lanplus -H " + host_ + " -U " + user_ + " -P '" + pass_ + "' -N 1 -R 1 raw " + 
              rawCommand + " > /dev/null 2>&1";
    }
    
    (void)system(cmd.c_str());
}

} // namespace temper
