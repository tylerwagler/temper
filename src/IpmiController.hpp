#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>

namespace temper {

struct IpmiMetrics {
    unsigned int inletTemp;
    unsigned int exhaustTemp;
    unsigned int powerConsumption; // Watts
    std::vector<unsigned int> fanSpeeds; // RPM
    std::vector<unsigned int> cpuTemps;  // RPM
    unsigned int targetFanSpeed; // %
    bool available;
};

class IpmiController {
public:
    IpmiController();
    ~IpmiController();
    
    void init(const std::string& host, const std::string& user, const std::string& pass);
    void setUseSsh(bool useSsh) { useSsh_ = useSsh; }
    void setChassisFanSpeed(unsigned int speedPercent);
    void pollMetrics();
    IpmiMetrics getMetrics() const;
    
    bool isEnabled() const { return !host_.empty(); }
    bool isPolling() const { return polling_.load(); }

private:
    std::string host_;
    std::string user_;
    std::string pass_;
    bool useSsh_ = false;
    
    IpmiMetrics metrics_;
    mutable std::mutex metricsMutex_;
    std::atomic<bool> polling_;
    
    void executeRaw(const std::string& rawCommand);
};

} // namespace temper
