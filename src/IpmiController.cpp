#include "IpmiController.hpp"
#include "ProcessUtils.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <array>
#include <memory>
#include <algorithm>
#include <thread>
#include <chrono>
#include <mutex>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

namespace temper {

IpmiController::IpmiController() : polling_(false), stopRequested_(false) {
    metrics_.inletTemp = 0;
    metrics_.exhaustTemp = 0;
    metrics_.powerConsumption = 0;
    metrics_.targetFanSpeed = 0;
    metrics_.available = false;
    metrics_.psu1Current = 0.0f;
    metrics_.psu2Current = 0.0f;
    metrics_.psu1Voltage = 0.0f;
    metrics_.psu2Voltage = 0.0f;
}

IpmiController::~IpmiController() {
    stopRequested_.store(true);
    if (pollingThread_ && pollingThread_->joinable()) {
        pollingThread_->join();
    }
    // cleanupPasswordFile(); // Not using password file
}

void IpmiController::init(const std::string& host, const std::string& user, const std::string& pass) {
    host_ = host;
    user_ = user;
    pass_ = pass;

    if (!host_.empty()) {
        // Don't use password file - inline password is fine
        // createPasswordFile();
        useFreeIPMI_ = detectFreeIPMI();
        if (useFreeIPMI_) {
            std::cout << "[IPMI] Controller initialized for host: " << host_ << " (using FreeIPMI)" << std::endl;
        } else {
            std::cout << "[IPMI] Controller initialized for host: " << host_ << " (using ipmitool)" << std::endl;
        }
    }
}

void IpmiController::createPasswordFile() {
    // Create secure password file to avoid exposing password in process list
    passwordFile_ = "/tmp/ipmi_pw_" + std::to_string(getpid());

    try {
        std::ofstream pwFile(passwordFile_, std::ios::out | std::ios::trunc);
        if (!pwFile.is_open()) {
            std::cerr << "[IPMI] Failed to create password file: " << passwordFile_ << std::endl;
            passwordFile_.clear();
            return;
        }

        pwFile << pass_;
        pwFile.close();

        // Set permissions to 600 (owner read/write only)
        if (chmod(passwordFile_.c_str(), S_IRUSR | S_IWUSR) != 0) {
            std::cerr << "[IPMI] Failed to set password file permissions" << std::endl;
            cleanupPasswordFile();
            return;
        }

        if (std::getenv("VERBOSE")) {
            std::cout << "[IPMI] Created secure password file: " << passwordFile_ << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[IPMI] Exception creating password file: " << e.what() << std::endl;
        passwordFile_.clear();
    }
}

void IpmiController::cleanupPasswordFile() {
    if (!passwordFile_.empty()) {
        unlink(passwordFile_.c_str());
        passwordFile_.clear();
    }
}

// Removed buildIpmitoolArgs - now using FreeIPMI exclusively

void IpmiController::startAsyncPoll() {
    if (host_.empty() || polling_.load()) {
        return;  // Already polling or not configured
    }

    // Clean up previous thread if finished
    if (pollingThread_ && pollingThread_->joinable()) {
        pollingThread_->join();
    }

    // Start new polling thread
    pollingThread_ = std::make_unique<std::thread>([this]() {
        pollMetricsImpl();
    });
}

void IpmiController::waitForPollComplete() {
    if (pollingThread_ && pollingThread_->joinable()) {
        pollingThread_->join();
    }
}

bool IpmiController::detectFreeIPMI() {
    try {
        // Try to run ipmi-sensors --version to detect FreeIPMI
        ProcessResult result = executeSafe({"ipmi-sensors", "--version"}, 3);
        if (result.exitCode == 0) {
            if (std::getenv("VERBOSE")) {
                // Safely extract first part of output
                std::string msg = result.stdOut;
                if (msg.length() > 50) {
                    msg = msg.substr(0, 50) + "...";
                }
                std::cout << "[IPMI] FreeIPMI detected: " << msg << std::endl;
            }
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[IPMI] Exception detecting FreeIPMI: " << e.what() << std::endl;
    }
    return false;
}

bool IpmiController::queryWithFreeIPMI(IpmiMetrics& m) {
    // Use ipmi-sensors with CSV output for fast bulk query
    std::vector<std::string> args;
    args.push_back("ipmi-sensors");
    args.push_back("-h");
    args.push_back(host_);
    args.push_back("-u");
    args.push_back(user_);
    args.push_back("-p");
    args.push_back(pass_);
    args.push_back("--driver-type=LAN_2_0");
    args.push_back("-l");
    args.push_back("OPERATOR");
    args.push_back("--workaround-flags=authcap,idzero,unexpectedauth,forcepermsg");
    args.push_back("--session-timeout=20000");
    args.push_back("--retransmission-timeout=2000");
    args.push_back("--sdr-cache-recreate");
    args.push_back("--comma-separated-output");
    args.push_back("--output-sensor-state");
    args.push_back("--no-header-output");
    args.push_back("--quiet-cache");
    args.push_back("--ignore-not-available-sensors");
    args.push_back("--ignore-unrecognized-events");

    ProcessResult result = executeSafe(args, 25);

    if (result.exitCode != 0) {
        if (std::getenv("VERBOSE")) {
            std::cerr << "[IPMI] FreeIPMI query failed: " << result.stdErr << std::endl;
        }
        return false;
    }

    // Parse CSV output
    std::istringstream stream(result.stdOut);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        // Split CSV line
        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string field;

        while (std::getline(ss, field, ',')) {
            // Remove quotes if present - be careful with string length
            if (field.length() >= 2 && field.front() == '\'' && field.back() == '\'') {
                field = field.substr(1, field.length() - 2);
            }
            fields.push_back(std::move(field));  // Use move to avoid copy
        }

        // Need at least: ID, Name, Type, State, Reading, Units
        if (fields.size() < 6) continue;

        const std::string& name = fields[1];
        const std::string& type = fields[2];
        const std::string& reading = fields[4];

        // Parse reading as float (will work for both ints and decimals)
        float floatValue = 0.0f;
        try {
            floatValue = std::stof(reading);
        } catch (...) {
            continue;
        }

        // Map sensor names to metrics
        if (name == "Inlet Temp") {
            m.inletTemp = static_cast<unsigned int>(floatValue);
        } else if (name == "Exhaust Temp") {
            m.exhaustTemp = static_cast<unsigned int>(floatValue);
        } else if (name == "Pwr Consumption") {
            m.powerConsumption = static_cast<unsigned int>(floatValue);
        } else if (name.find("Temp") != std::string::npos && type == "Temperature") {
            // CPU temps
            m.cpuTemps.push_back(static_cast<unsigned int>(floatValue));
        } else if (type == "Fan") {
            // Fan speeds
            m.fanSpeeds.push_back(static_cast<unsigned int>(floatValue));
        } else if (name == "Current 1" && type == "Current") {
            m.psu1Current = floatValue;
        } else if (name == "Current 2" && type == "Current") {
            m.psu2Current = floatValue;
        } else if (name == "Voltage 1" && type == "Voltage") {
            m.psu1Voltage = floatValue;
        } else if (name == "Voltage 2" && type == "Voltage") {
            m.psu2Voltage = floatValue;
        }
    }

    return m.inletTemp > 0;  // Success if we got at least inlet temp
}

// Removed queryAllSensorsBulk and querySensorWithRetry - now using FreeIPMI exclusively

void IpmiController::pollMetricsImpl() {
    if (host_.empty() || polling_.exchange(true)) {
        return;
    }

    // RAII guard to reset polling flag
    struct PollingGuard {
        std::atomic<bool>& flag;
        ~PollingGuard() { flag.store(false); }
    } guard{polling_};

    if (std::getenv("VERBOSE")) {
        std::cout << "[IPMI] Starting metrics poll..." << std::endl;
    }

    auto startTime = std::chrono::steady_clock::now();

    IpmiMetrics m;
    m.available = false;
    m.inletTemp = 0;
    m.exhaustTemp = 0;
    m.powerConsumption = 0;
    m.targetFanSpeed = 0;
    m.fanSpeeds.clear();
    m.cpuTemps.clear();
    m.psu1Current = 0.0f;
    m.psu2Current = 0.0f;
    m.psu1Voltage = 0.0f;
    m.psu2Voltage = 0.0f;

    bool success = false;

    // Try FreeIPMI (fastest method)
    if (useFreeIPMI_) {
        success = queryWithFreeIPMI(m);
        if (success && m.inletTemp > 0) {
            m.available = true;
        }
    }

    // If FreeIPMI fails, log and continue with empty metrics
    if (!success && std::getenv("VERBOSE")) {
        std::cerr << "[IPMI] FreeIPMI query failed" << std::endl;
    }

    // Preserve target fan speed from previous metrics
    {
        std::lock_guard<std::mutex> lock(metricsMutex_);
        m.targetFanSpeed = metrics_.targetFanSpeed;
    }

    // Update metrics atomically
    {
        std::lock_guard<std::mutex> lock(metricsMutex_);
        metrics_ = m;
    }

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    if (m.available) {
        std::cout << "[IPMI] Poll successful (" << duration << "ms). "
                  << "Inlet: " << m.inletTemp << "°C, "
                  << "Exhaust: " << m.exhaustTemp << "°C, "
                  << "Power: " << m.powerConsumption << "W, "
                  << "Fans: " << m.fanSpeeds.size() << ", "
                  << "CPUs: " << m.cpuTemps.size() << std::endl;
    } else {
        std::cerr << "[IPMI] Poll failed after " << duration << "ms (all sensors unavailable)" << std::endl;
    }
}

IpmiMetrics IpmiController::getMetrics() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return metrics_;
}

void IpmiController::setChassisFanSpeed(unsigned int speedPercent) {
    if (host_.empty()) {
        return;
    }

    // Update target in metrics
    {
        std::lock_guard<std::mutex> lock(metricsMutex_);
        metrics_.targetFanSpeed = speedPercent;
    }

    // Set guard to prevent concurrent operations
    bool wasPolling = polling_.exchange(true);
    if (wasPolling) {
        return;  // Another operation in progress
    }

    struct Guard {
        std::atomic<bool>& flag;
        ~Guard() { flag.store(false); }
    } guard{polling_};

    // Enable manual fan control
    executeRaw({"0x30", "0x30", "0x01", "0x00"});

    // Set fan speed
    std::stringstream ss;
    ss << "0x" << std::hex << speedPercent;
    executeRaw({"0x30", "0x30", "0x02", "0xff", ss.str()});

    if (std::getenv("VERBOSE")) {
        std::cout << "[IPMI] Set chassis fan speed to " << speedPercent << "%" << std::endl;
    }
}

void IpmiController::executeRaw(const std::vector<std::string>& rawArgs) {
    if (host_.empty()) return;

    // Use FreeIPMI's ipmi-raw command
    std::vector<std::string> args = {
        "ipmi-raw",
        "-h", host_,
        "-u", user_,
        "-p", pass_,
        "--driver-type=LAN_2_0",
        "-l", "OPERATOR",
        "--workaround-flags=authcap,idzero,unexpectedauth,forcepermsg",
        "--session-timeout=20000",
        "--retransmission-timeout=2000"
    };

    // Add raw command bytes
    args.insert(args.end(), rawArgs.begin(), rawArgs.end());

    ProcessResult result = executeSafe(args, 10);

    if (result.exitCode != 0 && std::getenv("VERBOSE")) {
        std::cerr << "[IPMI] Raw command failed: " << result.stdErr << std::endl;
    }
}

} // namespace temper
