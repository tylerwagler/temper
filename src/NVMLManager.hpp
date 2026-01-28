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
        unsigned int graphics = 0;
        unsigned int memory = 0;
        unsigned int sm = 0;
        unsigned int video = 0;
        unsigned int maxGraphics = 0;
        unsigned int maxMemory = 0;
        unsigned int maxSm = 0;
        unsigned int maxVideo = 0;
    };
    
    struct PcieInfo {
        unsigned int txThroughput = 0; // KB/s
        unsigned int rxThroughput = 0; // KB/s
        unsigned int gen = 0;
        unsigned int width = 0;
    };

    struct EccCounts {
        unsigned long long volatileSingle = 0;
        unsigned long long volatileDouble = 0;
        unsigned long long aggregateSingle = 0;
        unsigned long long aggregateDouble = 0;
    };

    struct ProcessInfo {
        unsigned int pid = 0;
        unsigned long long usedMemory = 0; // Bytes
        std::string name = "";
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
    void getPowerConstraints(nvmlDevice_t handle, unsigned int& minW, unsigned int& maxW) const;
    void restoreAutoFans(nvmlDevice_t handle);
    unsigned long long getThrottleReasons(nvmlDevice_t handle) const;

private:
    void checkResult(nvmlReturn_t result, const std::string& action) const;
};

} // namespace temper
