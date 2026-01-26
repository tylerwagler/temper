#pragma once

#include "Common.hpp"
#include <nvml.h>
#include <string>
#include <vector>
#include <stdexcept>

namespace temper {

class NVMLManager {
public:
    NVMLManager();
    ~NVMLManager();

    // No copying (NVML handles are sensitive)
    NVMLManager(const NVMLManager&) = delete;
    NVMLManager& operator=(const NVMLManager&) = delete;

    unsigned int getDeviceCount() const;
    nvmlDevice_t getHandle(unsigned int index) const;
    std::string getUUID(nvmlDevice_t handle) const;
    
    struct Clocks {
        unsigned int graphics;
        unsigned int memory;
        unsigned int sm;
        unsigned int video;
        unsigned int maxGraphics;
        unsigned int maxMemory;
        unsigned int maxSm;
        unsigned int maxVideo;
    };
    
    struct PcieInfo {
        unsigned int txThroughput; // KB/s
        unsigned int rxThroughput; // KB/s
        unsigned int gen;
        unsigned int width;
    };

    struct EccCounts {
        unsigned long long volatileSingle;
        unsigned long long volatileDouble;
        unsigned long long aggregateSingle;
        unsigned long long aggregateDouble;
    };

    struct ProcessInfo {
        unsigned int pid;
        unsigned long long usedMemory; // Bytes
        std::string name; // Need to fetch process name too if possible, or just PID
    };

    unsigned int getTemperature(nvmlDevice_t handle) const;
    unsigned int getFanSpeed(nvmlDevice_t handle) const;
    unsigned int getPowerUsage(nvmlDevice_t handle) const;
    unsigned int getPowerLimit(nvmlDevice_t handle) const;
    void getUtilization(nvmlDevice_t handle, unsigned int& gpu, unsigned int& memory) const;
    void getMemoryInfo(nvmlDevice_t handle, unsigned long long& total, unsigned long long& used) const;
    std::string getName(nvmlDevice_t handle) const;
    
    // Advanced Metrics
    Clocks getClocks(nvmlDevice_t handle) const;
    PcieInfo getPcieInfo(nvmlDevice_t handle) const;
    EccCounts getEccCounts(nvmlDevice_t handle) const;
    std::vector<ProcessInfo> getProcesses(nvmlDevice_t handle) const;
    std::string getVbiosVersion(nvmlDevice_t handle) const;
    std::string getSerial(nvmlDevice_t handle) const;
    unsigned int getPowerState(nvmlDevice_t handle) const; // P-State

    void setFanSpeed(nvmlDevice_t handle, unsigned int speedPercent);
    void setPowerLimit(nvmlDevice_t handle, unsigned int watts);
    void restoreAutoFans(nvmlDevice_t handle);
    unsigned long long getThrottleReasons(nvmlDevice_t handle) const;

private:
    void checkResult(nvmlReturn_t result, const std::string& action) const;
};

} // namespace temper
