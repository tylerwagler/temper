#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

namespace temper {

struct HostMetrics {
    double cpuUsagePercent; // 0-100%
    unsigned long long memTotal; // Bytes
    unsigned long long memAvailable; // Bytes
    double loadAvg1m;
    double loadAvg5m;
    double loadAvg15m;
    unsigned long long uptime; // Seconds
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

    HostMetrics metrics_;
    mutable std::mutex mutex_;
    
    // CPU Calculation State
    unsigned long long prevIdle_;
    unsigned long long prevTotal_;
};

} // namespace temper
