#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <map>

namespace temper {

struct ProcessInfo {
    unsigned int pid;
    unsigned long long usedMemory;
    std::string name;
};

struct GpuMetrics {
    unsigned int index;
    std::string name;
    std::string serial;
    std::string vbios;
    unsigned int pState;
    std::string pStateDescription; // e.g. "Max Performance"

    unsigned int temp;
    unsigned int fanSpeed;      // Actual reading
    unsigned int targetFan;     // What we set it to
    unsigned int powerUsage;    // mW
    unsigned int powerLimit;    // mW
    
    unsigned int utilGpu;       // %
    unsigned int utilMem;       // %
    
    unsigned long long memTotal; // Bytes
    unsigned long long memUsed;  // Bytes
    
    // Advanced
    unsigned int clockGraphics;
    unsigned int clockMemory;
    unsigned int clockSm;
    unsigned int clockVideo;
    unsigned int maxClockGraphics;
    unsigned int maxClockMemory;
    unsigned int maxClockSm;
    unsigned int maxClockVideo;
    
    unsigned int pcieTx; // KB/s
    unsigned int pcieRx; // KB/s
    unsigned int pcieGen;
    unsigned int pcieWidth;
    
    unsigned long long eccVolatileSingle;
    unsigned long long eccVolatileDouble;
    unsigned long long eccAggregateSingle;
    unsigned long long eccAggregateDouble;

    std::vector<ProcessInfo> processes;
    std::string throttleAlert;
    unsigned long long throttleReasonsBitmask;
};

class MetricServer {
public:
    MetricServer(int port);
    ~MetricServer();

    void start();
    void stop();
    void updateMetrics(const std::vector<GpuMetrics>& metrics);

private:
    void serverLoop();
    void handleClient(int clientSocket);
    std::string buildJson() const;

    int m_port;
    int m_serverSocket;
    std::atomic<bool> m_running;
    std::thread m_serverThread;

    std::vector<GpuMetrics> m_latestMetrics;
    mutable std::mutex m_metricsMutex;
};

} // namespace temper
