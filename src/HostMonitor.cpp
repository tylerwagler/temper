#include "HostMonitor.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unistd.h>

namespace temper {

HostMonitor::HostMonitor() 
    : prevIdle_(0), prevTotal_(0), metrics_{0} {
    // Initial read for CPU delta
    readCpuStats();
}

HostMonitor::~HostMonitor() {}

void HostMonitor::update() {
    std::lock_guard<std::mutex> lock(mutex_);
    readCpuStats();
    readMemStats();
    readLoadAvg();
    readUptime();
}

HostMetrics HostMonitor::getMetrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return metrics_;
}

void HostMonitor::readCpuStats() {
    std::ifstream file("/proc/stat");
    std::string line;
    if (std::getline(file, line)) {
        if (line.substr(0, 3) == "cpu") {
            std::istringstream iss(line);
            std::string cpu;
            iss >> cpu;
            
            unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
            iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
            
            unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
            unsigned long long totalIdle = idle + iowait;

            if (prevTotal_ > 0) {
                unsigned long long totalDiff = total - prevTotal_;
                unsigned long long idleDiff = totalIdle - prevIdle_;
                
                if (totalDiff > 0) {
                    metrics_.cpuUsagePercent = (double)(totalDiff - idleDiff) / totalDiff * 100.0;
                }
            }

            prevTotal_ = total;
            prevIdle_ = totalIdle;
        }
    }
}

void HostMonitor::readMemStats() {
    std::ifstream file("/proc/meminfo");
    std::string line;
    unsigned long long total = 0;
    unsigned long long available = 0;

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        unsigned long long value;
        std::string unit;
        
        iss >> key >> value >> unit;
        
        if (key == "MemTotal:") {
            total = value * 1024; // KB to Bytes
        } else if (key == "MemAvailable:") {
            available = value * 1024;
        }

        if (total > 0 && available > 0) break;
    }
    
    metrics_.memTotal = total;
    metrics_.memAvailable = available;
}

void HostMonitor::readLoadAvg() {
    std::ifstream file("/proc/loadavg");
    if (file) {
        file >> metrics_.loadAvg1m >> metrics_.loadAvg5m >> metrics_.loadAvg15m;
    }
}

void HostMonitor::readUptime() {
    std::ifstream file("/proc/uptime");
    double uptimeSecs;
    if (file >> uptimeSecs) {
        metrics_.uptime = (unsigned long long)uptimeSecs;
    }
}

} // namespace temper
