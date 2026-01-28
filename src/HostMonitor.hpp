#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

namespace temper {

struct HostMetrics {
    std::string hostname;
    double cpuUsagePercent = 0.0; // 0-100%
    unsigned long long memTotal = 0; // Bytes
    unsigned long long memAvailable = 0; // Bytes
    double loadAvg1m = 0.0;
    double loadAvg5m = 0.0;
    double loadAvg15m = 0.0;
    unsigned long long uptime = 0; // Seconds
};

class HostMonitor {
public:
    HostMonitor();
    ~HostMonitor();

    void update();
    HostMetrics getMetrics() const;

private:
    void readCpuStats();
    void readMemStats();
    void readLoadAvg();
    void readUptime();
    void readHostname();

    // CPU Calculation State (Move to top for init order)
    unsigned long long prevIdle_;
    unsigned long long prevTotal_;

    HostMetrics metrics_;
    mutable std::mutex mutex_;
};

} // namespace temper
