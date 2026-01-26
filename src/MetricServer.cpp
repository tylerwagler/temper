#include "MetricServer.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sstream>
#include <iostream>
#include <cstring>
#include <thread>
#include <algorithm>

namespace temper {

MetricServer::MetricServer(int port) : m_port(port), m_running(false), m_cachedJson("{}") {}

MetricServer::~MetricServer() {
    stop();
}

void MetricServer::start() {
    m_running = true;
    m_thread = std::thread(&MetricServer::loop, this);
    std::cout << "Metric Server started on port " << m_port << std::endl;
}

void MetricServer::stop() {
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void MetricServer::updateMetrics(const std::vector<GpuMetrics>& metrics, const HostMetrics& host, const IpmiMetrics& ipmi) {
    std::string json = buildJson(metrics, host, ipmi);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cachedJson = json;
}

std::string MetricServer::buildJson(const std::vector<GpuMetrics>& metrics, const HostMetrics& host, const IpmiMetrics& ipmi) {
    std::stringstream oss;
    oss << "{"
        << "\"host\": {"
            << "\"cpu_load_percent\":" << host.cpuUsagePercent << ","
            << "\"memory_total_mb\":" << (host.memTotal / 1024 / 1024) << ","
            << "\"memory_available_mb\":" << (host.memAvailable / 1024 / 1024) << ","
            << "\"load_avg_1m\":" << host.loadAvg1m << ","
            << "\"load_avg_5m\":" << host.loadAvg5m << ","
            << "\"uptime_seconds\":" << host.uptime
        << "},";
        
    oss << "\"chassis\": {"
            << "\"ipmi_available\":" << (ipmi.available ? "true" : "false") << ",";
    
    if (ipmi.available) {
        oss << "\"inlet_temp_c\":" << ipmi.inletTemp << ","
            << "\"exhaust_temp_c\":" << ipmi.exhaustTemp << ","
            << "\"power_consumption_w\":" << ipmi.powerConsumption << ","
            << "\"cpu_temps_c\": [";
            for (size_t i = 0; i < ipmi.cpuTemps.size(); ++i) {
                oss << ipmi.cpuTemps[i];
                if (i < ipmi.cpuTemps.size() - 1) oss << ",";
            }
        oss << "],"
            << "\"fans_rpm\": [";
            for (size_t i = 0; i < ipmi.fanSpeeds.size(); ++i) {
                oss << ipmi.fanSpeeds[i];
                if (i < ipmi.fanSpeeds.size() - 1) oss << ",";
            }
        oss << "],"
            << "\"target_fan_percent\":" << ipmi.targetFanSpeed;
    } else {
         oss << "\"error\": \"Query timed out or connection failed\"";
    }
    oss << "},";

    oss << "\"gpus\": [";
    for (size_t i = 0; i < metrics.size(); ++i) {
        const auto& m = metrics[i];
        oss << "{"
            << "\"index\":" << m.index << ","
            << "\"name\":\"" << m.name << "\","
            << "\"serial\":\"" << m.serial << "\","
            << "\"vbios\":\"" << m.vbios << "\","
            
            << "\"temperature\":" << m.temp << ","
            << "\"fan_speed_percent\":" << m.fanSpeed << ","
            << "\"target_fan_percent\":" << m.targetFan << ","
            
            << "\"power_usage_mw\":" << m.powerUsage << ","
            << "\"power_limit_mw\":" << m.powerLimit << ","
            
            << "\"resources\": {"
                << "\"gpu_load_percent\":" << m.utilGpu << ","
                << "\"memory_load_percent\":" << m.utilMem << ","
                << "\"memory_used_mb\":" << (m.memUsed / 1024 / 1024) << ","
                << "\"memory_total_mb\":" << (m.memTotal / 1024 / 1024)
            << "},"

             << "\"p_state\": {"
                << "\"id\":" << m.pState << ","
                << "\"description\":\"" << m.pStateDescription << "\""
            << "},"
            
            << "\"clocks\": {"
                << "\"graphics\":" << m.clockGraphics << ","
                << "\"memory\":" << m.clockMemory << ","
                << "\"sm\":" << m.clockSm << ","
                << "\"video\":" << m.clockVideo << ","
                << "\"max_graphics\":" << m.maxClockGraphics << ","
                << "\"max_memory\":" << m.maxClockMemory << ","
                << "\"max_sm\":" << m.maxClockSm << ","
                << "\"max_video\":" << m.maxClockVideo
            << "},"
            
            << "\"pcie\": {"
                << "\"tx_throughput_kbs\":" << m.pcieTx << ","
                << "\"rx_throughput_kbs\":" << m.pcieRx << ","
                << "\"gen\":" << m.pcieGen << ","
                << "\"width\":" << m.pcieWidth
            << "},"

            << "\"ecc\": {"
                << "\"volatile_single\":" << m.eccVolatileSingle << ","
                << "\"volatile_double\":" << m.eccVolatileDouble << ","
                << "\"aggregate_single\":" << m.eccAggregateSingle << ","
                << "\"aggregate_double\":" << m.eccAggregateDouble
            << "},"

            << "\"processes\": [";
            for (size_t j = 0; j < m.processes.size(); ++j) {
                oss << "{"
                    << "\"pid\":" << m.processes[j].pid << ","
                    << "\"name\":\"" << m.processes[j].name << "\","
                    << "\"used_memory\":" << m.processes[j].usedMemory
                    << "}";
                if (j < m.processes.size() - 1) oss << ",";
            }
        oss << "],"

            << "\"throttle_alert\":\"" << m.throttleAlert << "\","
            << "\"throttle_reason_bitmask\":" << m.throttleReasonsBitmask
            << "}";
        if (i < metrics.size() - 1) oss << ",";
    }
    oss << "]}";
    return oss.str();
}

void MetricServer::loop() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(m_port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(server_fd);
        return;
    }

    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        return;
    }

    while (m_running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(server_fd + 1, &readfds, NULL, NULL, &timeout);

        if (activity < 0) continue;

        if (FD_ISSET(server_fd, &readfds)) {
            socklen_t addrlen = sizeof(address);
            int new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
            if (new_socket < 0) continue;

            std::string body;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                body = m_cachedJson;
            }

            std::string response = 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Length: " + std::to_string(body.length()) + "\r\n"
                "\r\n" + 
                body;

            send(new_socket, response.c_str(), response.length(), 0);
            close(new_socket);
        }
    }
    close(server_fd);
}

} // namespace temper
