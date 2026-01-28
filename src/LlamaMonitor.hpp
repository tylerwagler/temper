#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace temper {

enum class LlamaStatus {
    OFFLINE,
    LOADING,
    READY,
    IDLE
};

struct LlamaSlotMetrics {
    int id = -1;
    int n_ctx = 0;
    int tokens_cached = 0;  // Legacy: kept for backward compatibility
    std::string state = "unknown";

    // Performance
    int prompt_n = 0;
    double prompt_ms = 0;
    int predicted_n = 0;
    double predicted_ms = 0;
    int cache_n = 0;

    // KV Cache Metrics (new)
    int kv_pos_min = -1;
    int kv_pos_max = -1;
    int kv_cells_used = 0;
    double kv_utilization = 0.0;
    double kv_cache_efficiency = 0.0;

    // Performance Metrics (new)
    double prompt_tokens_per_sec = 0.0;
    double generation_tokens_per_sec = 0.0;
    double speculative_acceptance_rate = 0.0;
    int draft_tokens_total = 0;
    int draft_tokens_accepted = 0;
};

struct LlamaMetrics {
    LlamaStatus status;
    std::string modelName;
    std::string modelPath;
    int slotsUsed;
    int slotsTotal;
    
    // Prometheus Metrics
    long long prompt_tokens_total;
    long long tokens_predicted_total;
    double prompt_seconds_total;
    double tokens_predicted_seconds_total;
    long long n_decode_total;
    double n_busy_slots_per_decode;
    
    double prompt_tokens_seconds;
    double predicted_tokens_seconds;
    double kv_cache_usage_ratio;
    long long kv_cache_tokens;
    int requests_processing;
    int requests_deferred;
    int n_tokens_max;
    
    // Props
    int n_ctx;
    
    // Per-slot metrics
    std::vector<LlamaSlotMetrics> slots;
};

class LlamaMonitor {
public:
    LlamaMonitor();
    ~LlamaMonitor();

    void start();
    void stop();

    LlamaMetrics getMetrics() const;

private:
    void pollLoop();
    void checkStatus();
    
    // Helper to execute curl with timeout
    // Returns: pair<exit_code, output>
    std::pair<int, std::string> executeCurl(const std::string& url, int timeoutSec);

    std::atomic<bool> running_;
    std::thread pollThread_;
    mutable std::mutex mutex_;
    
    LlamaMetrics metrics_;
    std::string host_ = "localhost";
    int port_ = 8081;
    std::string apiKey_ = "";
};

} // namespace temper
