#include <iostream>
#include <vector>
#include <csignal>
#include <unistd.h>
#include <iomanip>
#include <thread>
#include <chrono>

#include "MetricServer.hpp"
#include "NVMLManager.hpp"
#include "CurveController.hpp"
#include "IpmiController.hpp"
#include "HostMonitor.hpp"

using namespace temper;

// Global state for signal handling
static volatile std::sig_atomic_t g_running = 1;
static std::vector<nvmlDevice_t> g_devices;
static NVMLManager* g_nvmlPtr = nullptr;
static MetricServer* g_serverPtr = nullptr;

void signalHandler(int signum) {
    g_running = 0;
    if (g_serverPtr) g_serverPtr->stop();
    if (g_nvmlPtr) {
        for (auto dev : g_devices) {
            g_nvmlPtr->restoreAutoFans(dev);
        }
    }
    if (signum == SIGSEGV || signum == SIGBUS) _exit(128 + signum);
}

int main(int argc, char* argv[]) {
    try {
        NVMLManager nvml;
        g_nvmlPtr = &nvml;

        CurveController fanCurve;
        CurveController powerCurve;
        CurveController chassisCurve;
        
        // Start Metric Server
        MetricServer server(3001);
        g_serverPtr = &server;
        server.start();

        if (argc < 2) {
            std::cerr << "Usage: temper <command> [args...]" << std::endl;
            return 1;
        }

        std::string command = argv[1];
        if (command == "fanctl") {
            std::string fanArgs;
            for (int i = 2; i < argc; ++i) {
                fanArgs += argv[i];
                fanArgs += " ";
            }
            fanCurve.parseSetpoints(fanArgs);
            
            const char* pEnv = std::getenv("POWER_SETPOINTS");
            if (pEnv) powerCurve.parseSetpoints(pEnv);

            const char* cEnv = std::getenv("CHASSIS_FAN_SETPOINTS");
            if (cEnv) {
                chassisCurve.parseSetpoints(cEnv);
            } else {
                chassisCurve.parseSetpoints(fanArgs); // Default to GPU curve
            }

            unsigned int count = nvml.getDeviceCount();
            for (unsigned int i = 0; i < count; ++i) {
                g_devices.push_back(nvml.getHandle(i));
            }

            std::signal(SIGINT, signalHandler);
            std::signal(SIGTERM, signalHandler);

            std::cout << "Starting dynamic C++ control for " << count << " device(s)" << std::endl;
            
            // IPMI Controller
            IpmiController ipmi;
            char* idracIp = std::getenv("IDRAC_IP");
            char* idracUser = std::getenv("IDRAC_USER");
            char* idracPass = std::getenv("IDRAC_PASS");
            
            if (idracIp && idracUser && idracPass) {
                ipmi.init(idracIp, idracUser, idracPass);
                if (std::getenv("IDRAC_SSH") != nullptr) {
                    ipmi.setUseSsh(true);
                }
            }

            // Host Monitor
            HostMonitor hostMonitor;

            bool verbose = (std::getenv("VERBOSE") != nullptr);
            int loopCounter = 0;
            unsigned int lastChassisFan = 0;

            while (g_running) {
                loopCounter++;

                // 1. Poll Host Metrics (Fast)
                hostMonitor.update();
                HostMetrics hostMetrics = hostMonitor.getMetrics();

                // 2. Poll IPMI Metrics (Slow - Async)
                // Run at startup, then every 300 loops (30 seconds)
                if ((loopCounter == 1 || loopCounter % 300 == 0) && ipmi.isEnabled() && !ipmi.isPolling()) {
                    std::thread([&ipmi]() { ipmi.pollMetrics(); }).detach();
                }
                IpmiMetrics ipmiMetrics = ipmi.getMetrics();
                ipmiMetrics.targetFanSpeed = lastChassisFan;

                // 3. Poll NVML Metrics
                unsigned int maxTemp = 0;
                std::vector<GpuMetrics> currentMetrics;

                for (unsigned int i = 0; i < g_devices.size(); ++i) {
                    auto handle = g_devices[i];
                    unsigned int temp = nvml.getTemperature(handle);
                    if (temp > maxTemp) maxTemp = temp;
                    
                    unsigned int targetFan = fanCurve.interpolate(temp);
                    nvml.setFanSpeed(handle, targetFan);

                    std::string powerStr = "";
                    unsigned int currentPowerLimit = 0;
                    unsigned int currentPowerUsage = nvml.getPowerUsage(handle); // mW

                    if (!powerCurve.isEmpty()) {
                        unsigned int targetPower = powerCurve.interpolate(temp);
                        
                        unsigned long long reasons = nvml.getThrottleReasons(handle);
                        std::string alert = "";
                        if (reasons & nvmlClocksThrottleReasonSwThermalSlowdown || reasons & nvmlClocksThrottleReasonHwSlowdown) {
                            // Hardware is already panicking. React by cutting power even further.
                            targetPower = 100; // Force 100W safety limit
                            alert = "[REACTIVE FALLBACK: 100W]";
                        }

                        nvml.setPowerLimit(handle, targetPower);
                        currentPowerLimit = targetPower * 1000;

                        powerStr = "\tPower: " + std::to_string(targetPower) + "W" + (alert.empty() ? "" : " " + alert);
                    } else {
                        currentPowerLimit = nvml.getPowerLimit(handle);
                    }
                    
                    // Collect Full Telemetry
                    GpuMetrics m;
                    m.index = i;
                    m.name = nvml.getName(handle);
                    m.serial = nvml.getSerial(handle);
                    m.vbios = nvml.getVbiosVersion(handle);
                    m.pState = nvml.getPowerState(handle);
                    switch(m.pState) {
                        case 0: m.pStateDescription = "Maximum Performance"; break;
                        case 1: m.pStateDescription = "Performance"; break;
                        case 2: m.pStateDescription = "Balanced"; break;
                        case 5: m.pStateDescription = "Compute/Video"; break;
                        case 8: m.pStateDescription = "Idle/Low Power"; break;
                        case 15: m.pStateDescription = "Minimum Power"; break;
                        default: m.pStateDescription = "Unknown"; break;
                    }

                    m.temp = temp;
                    m.targetFan = targetFan;
                    m.fanSpeed = nvml.getFanSpeed(handle);
                    m.powerUsage = currentPowerUsage;
                    m.powerLimit = currentPowerLimit;
                    
                    nvml.getUtilization(handle, m.utilGpu, m.utilMem);
                    nvml.getMemoryInfo(handle, m.memTotal, m.memUsed);
                    
                    // Advanced Metrics
                    auto clocks = nvml.getClocks(handle);
                    m.clockGraphics = clocks.graphics;
                    m.clockMemory = clocks.memory;
                    m.clockSm = clocks.sm;
                    m.clockVideo = clocks.video;
                    m.maxClockGraphics = clocks.maxGraphics;
                    m.maxClockMemory = clocks.maxMemory;
                    m.maxClockSm = clocks.maxSm;
                    m.maxClockVideo = clocks.maxVideo;

                    auto pcie = nvml.getPcieInfo(handle);
                    m.pcieTx = pcie.txThroughput;
                    m.pcieRx = pcie.rxThroughput;
                    m.pcieGen = pcie.gen;
                    m.pcieWidth = pcie.width;

                    auto ecc = nvml.getEccCounts(handle);
                    m.eccVolatileSingle = ecc.volatileSingle;
                    m.eccVolatileDouble = ecc.volatileDouble;
                    m.eccAggregateSingle = ecc.aggregateSingle;
                    m.eccAggregateDouble = ecc.aggregateDouble;

                    auto procs = nvml.getProcesses(handle);
                    for (const auto& p : procs) {
                        m.processes.push_back({p.pid, p.usedMemory, p.name});
                    }

                    // Throttle Check
                    unsigned long long reasons = nvml.getThrottleReasons(handle);
                    if (reasons & nvmlClocksThrottleReasonSwThermalSlowdown) m.throttleAlert = "SW Thermal Slowdown";
                    else if (reasons & nvmlClocksThrottleReasonHwSlowdown) m.throttleAlert = "HW Thermal Slowdown";
                    m.throttleReasonsBitmask = reasons;
                    
                    currentMetrics.push_back(m);

                    if (verbose) std::cout << "[" << i << "] Temp: " << temp << "C \tFan: " << targetFan << "%" << powerStr << std::endl;
                }
                
                // Push unified metrics to server
                server.updateMetrics(currentMetrics, hostMetrics, ipmiMetrics);
                
                if (ipmi.isEnabled()) {
                    // Only update chassis fan every 100 loops (10 seconds)
                    if (loopCounter % 100 == 0) {
                        unsigned int cpuMaxTemp = 0;
                        for (unsigned int cpuT : ipmiMetrics.cpuTemps) {
                            if (cpuT > cpuMaxTemp) cpuMaxTemp = cpuT;
                        }

                        bool gpuStruggling = false;
                        for (const auto& m : currentMetrics) {
                            if (m.fanSpeed >= 95 || (m.throttleReasonsBitmask & (nvmlClocksThrottleReasonSwThermalSlowdown | nvmlClocksThrottleReasonHwSlowdown))) {
                                gpuStruggling = true;
                                break;
                            }
                        }

                        unsigned int targetTemp = cpuMaxTemp;
                        std::string source = "CPU";
                        if (gpuStruggling && maxTemp > targetTemp) {
                            targetTemp = maxTemp;
                            source = "GPU (Help Mode)";
                        }

                        unsigned int chassisFan = chassisCurve.interpolate(targetTemp);
                        lastChassisFan = chassisFan;
                        ipmi.setChassisFanSpeed(chassisFan);
                        if (verbose) std::cout << "[Chassis] " << source << " Max Temp: " << targetTemp << "C \tFan: " << chassisFan << "%" << std::endl;
                    }
                }

                if (verbose && isatty(STDOUT_FILENO)) {
                    std::cout << "\033[" << (g_devices.size() + (ipmi.isEnabled() ? 1 : 0)) << "A" << std::flush;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } else {
             std::cout << "Command '" << command << "' not fully implemented in C++ yet (Try fanctl)." << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
