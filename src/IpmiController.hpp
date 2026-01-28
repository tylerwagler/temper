#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <memory>

namespace temper {

struct IpmiMetrics {
    unsigned int inletTemp;
    unsigned int exhaustTemp;
    unsigned int powerConsumption; // Watts
    std::vector<unsigned int> fanSpeeds; // RPM
    std::vector<unsigned int> cpuTemps;  // Degrees C
    unsigned int targetFanSpeed; // %
    bool available;

    // Power supply metrics
    float psu1Current;   // Amps
    float psu2Current;   // Amps
    float psu1Voltage;   // Volts
    float psu2Voltage;   // Volts
};

class IpmiController {
public:
    IpmiController();
    ~IpmiController();

    void init(const std::string& host, const std::string& user, const std::string& pass);
    void setUseSsh(bool useSsh) { useSsh_ = useSsh; }
    void setChassisFanSpeed(unsigned int speedPercent);

    // Async polling interface
    void startAsyncPoll();
    bool isPolling() const { return polling_.load(); }
    void waitForPollComplete();

    IpmiMetrics getMetrics() const;

    bool isEnabled() const { return !host_.empty(); }

private:
    std::string host_;
    std::string user_;
    std::string pass_;
    std::string passwordFile_;
    bool useSsh_ = false;
    bool useFreeIPMI_ = false;

    IpmiMetrics metrics_;
    mutable std::mutex metricsMutex_;
    std::atomic<bool> polling_;
    std::atomic<bool> stopRequested_;

    std::unique_ptr<std::thread> pollingThread_;

    // Core polling logic
    void pollMetricsImpl();

    // FreeIPMI query (fastest method)
    bool queryWithFreeIPMI(IpmiMetrics& metrics);

    // Sensor query with retry (ipmitool)
    // Execute raw IPMI command (using FreeIPMI's ipmi-raw)
    void executeRaw(const std::vector<std::string>& rawArgs);

    // Password file management
    void createPasswordFile();
    void cleanupPasswordFile();

    // Detect if FreeIPMI is available
    bool detectFreeIPMI();
};

} // namespace temper
