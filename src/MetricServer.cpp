#include "MetricServer.hpp"
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>

namespace temper {

MetricServer::MetricServer(int port) : m_port(port), m_serverSocket(-1), m_running(false) {}

MetricServer::~MetricServer() {
    stop();
}

void MetricServer::start() {
    m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverSocket < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return;
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(m_port);

    if (bind(m_serverSocket, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Failed to bind to port " << m_port << std::endl;
        close(m_serverSocket);
        return;
    }

    if (listen(m_serverSocket, 3) < 0) {
        std::cerr << "Failed to listen on socket" << std::endl;
        close(m_serverSocket);
        return;
    }

    m_running = true;
    m_serverThread = std::thread(&MetricServer::serverLoop, this);
    std::cout << "Metric Server started on port " << m_port << std::endl;
}

void MetricServer::stop() {
    m_running = false;
    if (m_serverSocket >= 0) {
        shutdown(m_serverSocket, SHUT_RDWR);
        close(m_serverSocket);
        m_serverSocket = -1;
    }
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
}

void MetricServer::updateMetrics(const std::vector<GpuMetrics>& metrics) {
    std::lock_guard<std::mutex> lock(m_metricsMutex);
    m_latestMetrics = metrics;
}

void MetricServer::serverLoop() {
    while (m_running) {
        struct sockaddr_in clientAddress;
        socklen_t addrLen = sizeof(clientAddress);
        int clientSocket = accept(m_serverSocket, (struct sockaddr*)&clientAddress, &addrLen);
        
        if (clientSocket < 0) {
            if (m_running) {
                // If running, this might be a real error or just a timeout/interrupt
                // std::cerr << "Accept failed" << std::endl; 
            }
            continue;
        }

        handleClient(clientSocket);
    }
}

void MetricServer::handleClient(int clientSocket) {
    // Read request (we ignore content, just assume GET)
    char buffer[1024] = {0};
    read(clientSocket, buffer, 1024);

    std::string json = buildJson();

    std::string response = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Access-Control-Allow-Origin: *\r\n"
                           "Content-Length: " + std::to_string(json.length()) + "\r\n"
                           "\r\n" +
                           json;

    send(clientSocket, response.c_str(), response.length(), 0);
    close(clientSocket);
}

std::string MetricServer::buildJson() const {
    std::lock_guard<std::mutex> lock(m_metricsMutex);
    std::ostringstream oss;
    oss << "{\"gpus\":[";
    
    for (size_t i = 0; i < m_latestMetrics.size(); ++i) {
        const auto& m = m_latestMetrics[i];
        oss << "{"
            << "\"index\":" << m.index << ","
            << "\"name\":\"" << m.name << "\","
            << "\"serial\":\"" << m.serial << "\","
            << "\"vbios\":\"" << m.vbios << "\","
            << "\"p_state\":" << m.pState << ","
            
            << "\"temperature\":" << m.temp << ","
            << "\"fan_speed_percent\":" << m.fanSpeed << ","
            << "\"target_fan_percent\":" << m.targetFan << ","
            << "\"power_usage_mw\":" << m.powerUsage << ","
            << "\"power_limit_mw\":" << m.powerLimit << ","
            
            << "\"utilization\": {"
                << "\"gpu\":" << m.utilGpu << ","
                << "\"memory\":" << m.utilMem
            << "},"
            
            << "\"memory\": {"
                << "\"total\":" << m.memTotal << ","
                << "\"used\":" << m.memUsed
            << "},"
            
            << "\"clocks\": {"
                << "\"graphics\":" << m.clockGraphics << ","
                << "\"memory\":" << m.clockMemory << ","
                << "\"sm\":" << m.clockSm << ","
                << "\"video\":" << m.clockVideo << ","
                << "\"max_graphics\":" << m.maxClockGraphics << ","
                << "\"max_memory\":" << m.maxClockMemory
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
                oss << "{\"pid\":" << m.processes[j].pid 
                    << ",\"used_memory\":" << m.processes[j].usedMemory
                    << ",\"name\":\"" << m.processes[j].name << "\"}";
                if (j < m.processes.size() - 1) oss << ",";
            }
        oss << "],"

            << "\"throttle_alert\":\"" << m.throttleAlert << "\""
            << "}";
        if (i < m_latestMetrics.size() - 1) oss << ",";
    }
    oss << "]}";
    return oss.str();
}

} // namespace temper
