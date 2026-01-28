#include "NVMLManager.hpp"
#include <iostream>

namespace temper {

NVMLManager::NVMLManager() {
    checkResult(nvmlInit(), "Initialize NVML");
}

NVMLManager::~NVMLManager() {
    nvmlShutdown();
}

unsigned int NVMLManager::getDeviceCount() const {
    unsigned int count = 0;
    checkResult(nvmlDeviceGetCount(&count), "Get device count");
    return count;
}

nvmlDevice_t NVMLManager::getHandle(unsigned int index) const {
    nvmlDevice_t handle;
    checkResult(nvmlDeviceGetHandleByIndex(index, &handle), "Get device handle");
    return handle;
}

std::string NVMLManager::getUUID(nvmlDevice_t handle) const {
    char uuid[80];
    checkResult(nvmlDeviceGetUUID(handle, uuid, 80), "Get UUID");
    return std::string(uuid);
}

unsigned int NVMLManager::getTemperature(nvmlDevice_t handle) const {
    unsigned int temp = 0;
    checkResult(nvmlDeviceGetTemperature(handle, NVML_TEMPERATURE_GPU, &temp), "Get temperature");
    return temp;
}

unsigned int NVMLManager::getFanSpeed(nvmlDevice_t handle) const {
    unsigned int speed = 0;
    // Get speed of first fan (index 0) as proxy
    checkResult(nvmlDeviceGetFanSpeed_v2(handle, 0, &speed), "Get fan speed");
    return speed;
}

unsigned int NVMLManager::getPowerUsage(nvmlDevice_t handle) const {
    unsigned int power = 0;
    checkResult(nvmlDeviceGetPowerUsage(handle, &power), "Get power usage");
    return power; // milliWatts
}

unsigned int NVMLManager::getPowerLimit(nvmlDevice_t handle) const {
    unsigned int limit = 0;
    checkResult(nvmlDeviceGetEnforcedPowerLimit(handle, &limit), "Get power limit");
    return limit; // milliWatts
}

void NVMLManager::getUtilization(nvmlDevice_t handle, unsigned int& gpu, unsigned int& memory) const {
    nvmlUtilization_t util;
    checkResult(nvmlDeviceGetUtilizationRates(handle, &util), "Get utilization");
    gpu = util.gpu;
    memory = util.memory;
}

void NVMLManager::getMemoryInfo(nvmlDevice_t handle, unsigned long long& total, unsigned long long& used) const {
    nvmlMemory_t mem;
    checkResult(nvmlDeviceGetMemoryInfo(handle, &mem), "Get memory info");
    total = mem.total;
    used = mem.used;
}

std::string NVMLManager::getName(nvmlDevice_t handle) const {
    char name[NVML_DEVICE_NAME_BUFFER_SIZE];
    checkResult(nvmlDeviceGetName(handle, name, NVML_DEVICE_NAME_BUFFER_SIZE), "Get device name");
    return std::string(name);
}

NVMLManager::Clocks NVMLManager::getClocks(nvmlDevice_t handle) const {
    Clocks c;
    // Current
    nvmlDeviceGetClockInfo(handle, NVML_CLOCK_GRAPHICS, &c.graphics);
    nvmlDeviceGetClockInfo(handle, NVML_CLOCK_MEM, &c.memory);
    nvmlDeviceGetClockInfo(handle, NVML_CLOCK_SM, &c.sm);
    nvmlDeviceGetClockInfo(handle, NVML_CLOCK_VIDEO, &c.video);
    
    // Max
    nvmlDeviceGetMaxClockInfo(handle, NVML_CLOCK_GRAPHICS, &c.maxGraphics);
    nvmlDeviceGetMaxClockInfo(handle, NVML_CLOCK_MEM, &c.maxMemory);
    nvmlDeviceGetMaxClockInfo(handle, NVML_CLOCK_SM, &c.maxSm);
    nvmlDeviceGetMaxClockInfo(handle, NVML_CLOCK_VIDEO, &c.maxVideo);
    return c;
}

NVMLManager::PcieInfo NVMLManager::getPcieInfo(nvmlDevice_t handle) const {
    PcieInfo p;
    nvmlDeviceGetPcieThroughput(handle, NVML_PCIE_UTIL_TX_BYTES, &p.txThroughput); // KB/s
    nvmlDeviceGetPcieThroughput(handle, NVML_PCIE_UTIL_RX_BYTES, &p.rxThroughput); // KB/s
    nvmlDeviceGetCurrPcieLinkGeneration(handle, &p.gen);
    nvmlDeviceGetCurrPcieLinkWidth(handle, &p.width);
    return p;
}

NVMLManager::EccCounts NVMLManager::getEccCounts(nvmlDevice_t handle) const {
    EccCounts e;
    // Volatile (since boot)
    nvmlDeviceGetTotalEccErrors(handle, NVML_MEMORY_ERROR_TYPE_CORRECTED, NVML_VOLATILE_ECC, &e.volatileSingle);
    nvmlDeviceGetTotalEccErrors(handle, NVML_MEMORY_ERROR_TYPE_UNCORRECTED, NVML_VOLATILE_ECC, &e.volatileDouble);
    // Aggregate (lifetime)
    nvmlDeviceGetTotalEccErrors(handle, NVML_MEMORY_ERROR_TYPE_CORRECTED, NVML_AGGREGATE_ECC, &e.aggregateSingle);
    nvmlDeviceGetTotalEccErrors(handle, NVML_MEMORY_ERROR_TYPE_UNCORRECTED, NVML_AGGREGATE_ECC, &e.aggregateDouble);
    return e;
}

std::vector<NVMLManager::ProcessInfo> NVMLManager::getProcesses(nvmlDevice_t handle) const {
    std::vector<ProcessInfo> processes;
    unsigned int infoCount = 0;
    
    // First call to get count - check for compute processes
    nvmlReturn_t r = nvmlDeviceGetComputeRunningProcesses(handle, &infoCount, nullptr);
    if (r == NVML_SUCCESS && infoCount > 0) {
        std::vector<nvmlProcessInfo_t> infos(infoCount);
        r = nvmlDeviceGetComputeRunningProcesses(handle, &infoCount, infos.data());
        if (r == NVML_SUCCESS) {
            for (unsigned int i = 0; i < infoCount; ++i) {
                ProcessInfo p;
                p.pid = infos[i].pid;
                p.usedMemory = infos[i].usedGpuMemory;
                
                char name[256] = {0};
                if (nvmlSystemGetProcessName(p.pid, name, 256) == NVML_SUCCESS) {
                    p.name = std::string(name);
                } else {
                    p.name = "Unknown";
                }
                processes.push_back(p);
            }
        }
    }
    
    // Also check for Graphics processes if separate
    unsigned int gInfoCount = 0;
    r = nvmlDeviceGetGraphicsRunningProcesses(handle, &gInfoCount, nullptr);
    if (r == NVML_SUCCESS && gInfoCount > 0) {
        std::vector<nvmlProcessInfo_t> gInfos(gInfoCount);
        r = nvmlDeviceGetGraphicsRunningProcesses(handle, &gInfoCount, gInfos.data());
        if (r == NVML_SUCCESS) {
             for (unsigned int i = 0; i < gInfoCount; ++i) {
                ProcessInfo p;
                p.pid = gInfos[i].pid;
                p.usedMemory = gInfos[i].usedGpuMemory;
                 char name[256] = {0};
                if (nvmlSystemGetProcessName(p.pid, name, 256) == NVML_SUCCESS) {
                    p.name = std::string(name);
                } else {
                    p.name = "Unknown";
                }
                processes.push_back(p);
            }
        }
    }

    return processes;
}

std::string NVMLManager::getVbiosVersion(nvmlDevice_t handle) const {
    char version[NVML_DEVICE_VBIOS_VERSION_BUFFER_SIZE];
    if (nvmlDeviceGetVbiosVersion(handle, version, NVML_DEVICE_VBIOS_VERSION_BUFFER_SIZE) == NVML_SUCCESS) {
        return std::string(version);
    }
    return "Unknown";
}

std::string NVMLManager::getSerial(nvmlDevice_t handle) const {
    char serial[NVML_DEVICE_SERIAL_BUFFER_SIZE];
    if (nvmlDeviceGetSerial(handle, serial, NVML_DEVICE_SERIAL_BUFFER_SIZE) == NVML_SUCCESS) {
        return std::string(serial);
    }
    return "Unknown";
}

unsigned int NVMLManager::getPowerState(nvmlDevice_t handle) const {
    nvmlPstates_t pState;
    if (nvmlDeviceGetPerformanceState(handle, &pState) == NVML_SUCCESS) {
        return (unsigned int)pState;
    }
    return 999;
}

void NVMLManager::setFanSpeed(nvmlDevice_t handle, unsigned int speedPercent) {
    unsigned int numFans = 0;
    nvmlDeviceGetNumFans(handle, &numFans);
    for (unsigned int i = 0; i < numFans; ++i) {
        checkResult(nvmlDeviceSetFanSpeed_v2(handle, i, speedPercent), "Set fan speed");
    }
}

void NVMLManager::setPowerLimit(nvmlDevice_t handle, unsigned int watts) {
    // NVML uses milliwatts
    checkResult(nvmlDeviceSetPowerManagementLimit(handle, watts * 1000), "Set power limit");
}

void NVMLManager::getPowerConstraints(nvmlDevice_t handle, unsigned int& minW, unsigned int& maxW) const {
    unsigned int minMW = 0, maxMW = 0;
    checkResult(nvmlDeviceGetPowerManagementLimitConstraints(handle, &minMW, &maxMW), "Get power constraints");
    minW = minMW / 1000;
    maxW = maxMW / 1000;
}

void NVMLManager::restoreAutoFans(nvmlDevice_t handle) {
    unsigned int numFans = 0;
    if (nvmlDeviceGetNumFans(handle, &numFans) == NVML_SUCCESS) {
        for (unsigned int i = 0; i < numFans; ++i) {
            nvmlDeviceSetFanControlPolicy(handle, i, NVML_FAN_POLICY_TEMPERATURE_CONTINOUS_SW);
        }
    }
}

unsigned long long NVMLManager::getThrottleReasons(nvmlDevice_t handle) const {
    unsigned long long reasons = 0;
    checkResult(nvmlDeviceGetCurrentClocksThrottleReasons(handle, &reasons), "Get throttle reasons");
    return reasons;
}

void NVMLManager::checkResult(nvmlReturn_t result, const std::string& action) const {
    if (result != NVML_SUCCESS) {
        throw std::runtime_error(action + " failed: " + nvmlErrorString(result));
    }
}

} // namespace temper
