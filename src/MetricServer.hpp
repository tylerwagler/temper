#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <map>

#include "HostMonitor.hpp" // New Include
#include "IpmiController.hpp" // New Include

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
    std::string pStateDescription; 

    unsigned int temp;
    unsigned int fanSpeed;      
    unsigned int targetFan;     
    unsigned int powerUsage;    
    unsigned int powerLimit;    
    
    unsigned int utilGpu;       
    unsigned int utilMem;       
    
    unsigned long long memTotal; 
    unsigned long long memUsed;  
    
    // Advanced
    unsigned int clockGraphics;
    unsigned int clockMemory;
    unsigned int clockSm;
    unsigned int clockVideo;
    unsigned int maxClockGraphics;
    unsigned int maxClockMemory;
    unsigned int maxClockSm;
    unsigned int maxClockVideo;
    
    unsigned int pcieTx; 
    unsigned int pcieRx; 
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
    // Updated Signature
    void updateMetrics(const std::vector<GpuMetrics>& metrics, const HostMetrics& host, const IpmiMetrics& ipmi);

private:
    void loop(); // Was serverLoop but cpp uses loop()
    // handleClient was in hpp but not in cpp implementation provided earlier? 
    // The cpp implementation provided earlier used a single loop method. 
    // To match the cpp I wrote in step 4306, I should stick to that interface.
    // The cpp I wrote uses `start` -> `loop` thread.
    // And `updateMetrics` updates `m_cachedJson`.
    
    // Check cpp implementation again? 
    // Step 4306: MetricServer::loop() { ... select() ... }
    // It does not use handleClient or serverLoop names. it uses `loop`.
    
    std::string buildJson(const std::vector<GpuMetrics>& metrics, const HostMetrics& host, const IpmiMetrics& ipmi);

    int m_port;
    std::atomic<bool> m_running;
    std::thread m_thread;

    std::string m_cachedJson;
    mutable std::mutex m_mutex;
};

} // namespace temper
