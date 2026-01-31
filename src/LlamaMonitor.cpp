#include "LlamaMonitor.hpp"
#include "ProcessUtils.hpp"
#include <iostream>
#include <sstream>
#include <array>
#include <memory>
#include <chrono>
#include <utility>
#include <mutex>

namespace temper {

LlamaMonitor::LlamaMonitor() : running_(false) {
    metrics_.status = LlamaStatus::OFFLINE;
    metrics_.slotsUsed = 0;
    metrics_.slotsTotal = 0;
    metrics_.modelPath = "";
    metrics_.load_progress = 0.0;

    metrics_.prompt_tokens_total = 0;
    metrics_.tokens_predicted_total = 0;
    metrics_.prompt_seconds_total = 0;
    metrics_.tokens_predicted_seconds_total = 0;
    metrics_.n_decode_total = 0;
    metrics_.n_busy_slots_per_decode = 0;

    metrics_.prompt_tokens_seconds = 0;
    metrics_.predicted_tokens_seconds = 0;
    metrics_.kv_cache_usage_ratio = 0;
    metrics_.kv_cache_tokens = 0;
    metrics_.requests_processing = 0;
    metrics_.requests_deferred = 0;
    metrics_.n_tokens_max = 0;
    metrics_.n_ctx = 0;

    const char* hostEnv = std::getenv("LLAMA_HOST");
    if (hostEnv) host_ = hostEnv;

    const char* apiKeyEnv = std::getenv("LLAMA_API_KEY");
    if (apiKeyEnv) apiKey_ = apiKeyEnv;
}

LlamaMonitor::~LlamaMonitor() {
    stop();
}

void LlamaMonitor::start() {
    running_ = true;
    pollThread_ = std::thread(&LlamaMonitor::pollLoop, this);
}

void LlamaMonitor::stop() {
    running_ = false;
    if (pollThread_.joinable()) {
        pollThread_.join();
    }
}

LlamaMetrics LlamaMonitor::getMetrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return metrics_;
}

void LlamaMonitor::pollLoop() {
    while (running_) {
        checkStatus();
        // Poll at 10Hz to match NVML and llama.cpp --poll 100
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// Executes curl safely and returns {exit_code, output}
std::pair<int, std::string> LlamaMonitor::executeCurl(const std::string& url, int timeoutSec) {
    std::vector<std::string> args = {
        "curl", "-s", "-f", "--max-time", std::to_string(timeoutSec)
    };

    if (!apiKey_.empty()) {
        args.push_back("-H");
        args.push_back("Authorization: Bearer " + apiKey_);
    }

    args.push_back(url);

    ProcessResult res = executeSafe(args, timeoutSec + 5);
    
    return { res.exitCode, res.stdOut };
}

// URL encode string for use in query parameters
static std::string urlEncode(const std::string& str) {
    std::string encoded;
    for (unsigned char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += "%20";
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}

static double parseMetric(const std::string& input, const std::string& key) {
    // Search for the metric line (not in comments)
    // Format: "key value" or "key{labels} value"
    // We need to find the key at the start of a line (not after "# HELP" or "# TYPE")

    size_t searchPos = 0;
    while (true) {
        size_t pos = input.find(key, searchPos);
        if (pos == std::string::npos) return 0.0;

        // Check if this is at the start of a line (pos == 0 or preceded by newline)
        // and not part of a comment line
        bool atLineStart = (pos == 0 || input[pos - 1] == '\n');

        if (atLineStart) {
            // Found the metric line, now parse the value
            // Skip to the space after the key (or after the labels)
            size_t endOfKey = input.find(' ', pos);
            if (endOfKey == std::string::npos) return 0.0;

            // The value might be separated by labels in {}
            size_t brace = input.find('{', pos);
            if (brace != std::string::npos && brace < endOfKey) {
                size_t endBrace = input.find('}', brace);
                if (endBrace != std::string::npos) {
                    endOfKey = input.find(' ', endBrace);
                }
            }

            if (endOfKey == std::string::npos) return 0.0;

            try {
                return std::stod(input.substr(endOfKey + 1));
            } catch (...) {
                return 0.0;
            }
        }

        // Not at line start, continue searching
        searchPos = pos + 1;
    }

    return 0.0;
}

// Simple JSON value extractor - finds "key": value or "key": "value"
// Returns empty string if not found
static std::string extractJsonValue(const std::string& json, const std::string& key, size_t startPos = 0) {
    std::string searchKey = "\"" + key + "\":";
    size_t keyPos = json.find(searchKey, startPos);
    if (keyPos == std::string::npos) return "";

    size_t valStart = keyPos + searchKey.length();
    // Skip whitespace
    while (valStart < json.length() && (json[valStart] == ' ' || json[valStart] == '\t')) valStart++;

    if (valStart >= json.length()) return "";

    // Check if value is quoted string
    if (json[valStart] == '"') {
        size_t valEnd = json.find('"', valStart + 1);
        if (valEnd == std::string::npos) return "";
        return json.substr(valStart + 1, valEnd - valStart - 1);
    }

    // Numeric or boolean value - read until comma, }, or ]
    size_t valEnd = valStart;
    while (valEnd < json.length() && json[valEnd] != ',' && json[valEnd] != '}' && json[valEnd] != ']' && json[valEnd] != '\n') {
        valEnd++;
    }

    return json.substr(valStart, valEnd - valStart);
}

// Extract numeric value from JSON, with default fallback
static double extractJsonNumber(const std::string& json, const std::string& key, size_t startPos = 0, double defaultVal = 0.0) {
    std::string val = extractJsonValue(json, key, startPos);
    if (val.empty()) return defaultVal;
    try {
        return std::stod(val);
    } catch (...) {
        return defaultVal;
    }
}

// Extract integer value from JSON
static int extractJsonInt(const std::string& json, const std::string& key, size_t startPos = 0, int defaultVal = 0) {
    std::string val = extractJsonValue(json, key, startPos);
    if (val.empty()) return defaultVal;
    try {
        return std::stoi(val);
    } catch (...) {
        return defaultVal;
    }
}

void LlamaMonitor::checkStatus() {
    LlamaMetrics m;
    
    // Initialize with current metrics to persist values on failure
    {
        std::lock_guard<std::mutex> lock(mutex_);
        m = metrics_;
    }

    // Get configured host or default to localhost
    const char* envHost = std::getenv("LLAMA_HOST");
    std::string llamaHost = envHost ? envHost : "localhost";
    
    const char* envPort = std::getenv("LLAMA_PORT");
    std::string llamaPort = envPort ? envPort : "8081";
    
    const char* envPrefix = std::getenv("LLAMA_API_PREFIX");
    std::string llamaPrefix = envPrefix ? envPrefix : "";
    
    std::string baseUrl = "http://" + llamaHost + ":" + llamaPort + llamaPrefix;

    // 1. Check /health (Fast check for offline vs online)
    // Timeout of 1s is sufficient for localhost
    auto healthRes = executeCurl(baseUrl + "/health", 1);
    
    // Helper to get active model ID and Status from /v1/models
    // Returns pair<exit_code, output> from curl
    auto getModelsJson = [&]() -> std::pair<int, std::string> {
        return executeCurl(baseUrl + "/v1/models", 10);
    };

    if (healthRes.first != 0) {
        // Health check failed. If it's a hard failure, we might want to reset.
        // But if it's just a timeout under load, maybe we persist?
        // Let's stick to the plan: if offline/unreachable, we set OFFLINE.
        m = LlamaMetrics{}; 
        m.status = LlamaStatus::OFFLINE;
        m.modelName = "Unknown";
    } else {
        // Health check passed. Now check models.
        auto modelsRes = getModelsJson();
        
        if (modelsRes.first != 0) {
            // Failed to get models (timeout or error). 
            // ABORT UPDATE. Keep 'm' as initialized (previous valid state).
            // We assume the server is just busy.
            return; 
        }

        std::string modelsJson = modelsRes.second;
        
        // Simple string finding to avoid full JSON parser dependency complexity
        // Look for "status": ... "value": "loading" or "loaded"
        // And "id": "..."
        // This is a bit hacky but robust enough if we just want to know if *anything* is loaded/loading.
        
        bool foundLoading = false;
        bool foundLoaded = false;
        std::string detectedId = "";

        // Find the model ID that corresponds to the loading or loaded status
        // Look for pattern: "id": "MODEL_NAME", ... "status": {"value": "loading"|"loaded"}
        size_t searchPos = 0;
        while (searchPos < modelsJson.length()) {
            size_t idPos = modelsJson.find("\"id\":", searchPos);
            if (idPos == std::string::npos) break;

            // Extract the ID
            size_t q1 = modelsJson.find("\"", idPos + 5);
            size_t q2 = modelsJson.find("\"", q1 + 1);
            if (q1 == std::string::npos || q2 == std::string::npos) break;

            std::string candidateId = modelsJson.substr(q1 + 1, q2 - q1 - 1);

            // Find the next status field (should be in the same model object)
            size_t statusPos = modelsJson.find("\"status\":", q2);
            if (statusPos == std::string::npos || statusPos > q2 + 500) {
                searchPos = q2;
                continue;
            }

            // Check if this model is loading or loaded
            size_t valuePos = modelsJson.find("\"value\":", statusPos);
            if (valuePos != std::string::npos && valuePos < statusPos + 100) {
                size_t v1 = modelsJson.find("\"", valuePos + 8);
                size_t v2 = modelsJson.find("\"", v1 + 1);
                if (v1 != std::string::npos && v2 != std::string::npos) {
                    std::string statusValue = modelsJson.substr(v1 + 1, v2 - v1 - 1);
                    if (statusValue == "loading") {
                        foundLoading = true;
                        detectedId = candidateId;
                        break;
                    } else if (statusValue == "loaded" || statusValue == "ready") {
                        foundLoaded = true;
                        detectedId = candidateId;
                        break;
                    }
                }
            }

            searchPos = q2;
        }

        if (!foundLoading && !foundLoaded) {
             // Success response but no loaded/loading models found.
             m.status = LlamaStatus::IDLE;
             m.modelName = "None";
             m.load_progress = 0.0;
        } else {
             if (detectedId.empty()) detectedId = "Unknown";
             m.modelName = detectedId;

             if (foundLoading) {
                 m.status = LlamaStatus::LOADING;
                 m.modelName = detectedId;

                 // Extract load_progress from the status object in modelsJson
                 // The statusPos variable from above points to the "status": position
                 // We need to find "load_progress" within that status object
                 size_t statusPos = modelsJson.find("\"status\":", modelsJson.find("\"id\":\"" + detectedId));
                 if (statusPos != std::string::npos) {
                     // Find the end of the status object (next closing brace)
                     size_t statusEnd = modelsJson.find("}", statusPos);
                     if (statusEnd != std::string::npos) {
                         std::string statusBlock = modelsJson.substr(statusPos, statusEnd - statusPos);
                         double progress = extractJsonNumber(statusBlock, "load_progress", 0, 0.0);
                         m.load_progress = progress;
                     } else {
                         m.load_progress = 0.0;
                     }
                 } else {
                     m.load_progress = 0.0;
                 }

                 if (std::getenv("VERBOSE")) {
                     std::cout << "[Llama] Model loading: " << detectedId
                               << " (" << (m.load_progress * 100.0) << "%)" << std::endl;
                 }
             } else {
                 // Loaded.
                 m.status = LlamaStatus::READY;
                 m.modelName = detectedId;
                 m.load_progress = 1.0;  // Fully loaded

                 if (std::getenv("VERBOSE")) {
                     std::cout << "[Llama] Model loaded: " << detectedId << std::endl;
                 }

                 // Poll Slots
                 std::string slotsUrl = baseUrl + "/slots?model=" + urlEncode(detectedId);
                 auto slotsRes = executeCurl(slotsUrl, 10); // 10s timeout
            
                 if (slotsRes.first == 0) {
                      std::string output = slotsRes.second;
                      
                      // Parse Slots
                      int count = 0;
                      size_t pos = 0;
                      while((pos = output.find("\"id\":", pos)) != std::string::npos) {
                          count++;
                          pos += 5;
                      }
                      m.slotsTotal = count;
                 
                      int used = 0;
                      pos = 0;
                      while((pos = output.find("\"state\":", pos)) != std::string::npos) {
                           char stateChar = output[pos + 8];
                           if (stateChar != '0') used++;
                           pos += 8;
                      }
                      m.slotsUsed = used;

                      // Parse per-slot metrics from /slots JSON response
                      m.slots.clear();

                      // The output variable contains the JSON response from /slots
                      // Parse each slot object: [{"id":0,"state":"idle",...},{"id":1,...},...]
                      size_t slotPos = 0;
                      while (true) {
                          // Find next slot object starting with "id":
                          slotPos = output.find("\"id\":", slotPos);
                          if (slotPos == std::string::npos) break;

                          // Find the end of this slot object (look for next "id": or end of array)
                          size_t nextSlotPos = output.find("\"id\":", slotPos + 5);
                          size_t endPos = (nextSlotPos != std::string::npos) ? nextSlotPos : output.length();

                          // Extract this slot's JSON substring
                          std::string slotJson = output.substr(slotPos - 1, endPos - (slotPos - 1));

                          // Parse basic slot fields
                          LlamaSlotMetrics slot;
                          slot.id = extractJsonInt(slotJson, "id");
                          slot.n_ctx = extractJsonInt(slotJson, "n_ctx");
                          slot.state = extractJsonValue(slotJson, "state");
                          slot.prompt_n = extractJsonInt(slotJson, "prompt_n");
                          slot.prompt_ms = extractJsonNumber(slotJson, "prompt_ms");
                          slot.predicted_n = extractJsonInt(slotJson, "predicted_n");
                          slot.predicted_ms = extractJsonNumber(slotJson, "predicted_ms");
                          slot.cache_n = extractJsonInt(slotJson, "cache_n");

                          // Parse kv_cache object if present
                          size_t kvCachePos = slotJson.find("\"kv_cache\":");
                          if (kvCachePos != std::string::npos) {
                              // Extract kv_cache nested object
                              size_t kvStart = slotJson.find("{", kvCachePos);
                              size_t kvEnd = slotJson.find("}", kvStart);
                              if (kvStart != std::string::npos && kvEnd != std::string::npos) {
                                  std::string kvJson = slotJson.substr(kvStart, kvEnd - kvStart + 1);

                                  slot.kv_pos_min = extractJsonInt(kvJson, "pos_min", 0, -1);
                                  slot.kv_pos_max = extractJsonInt(kvJson, "pos_max", 0, -1);
                                  slot.kv_cells_used = extractJsonInt(kvJson, "cells_used");
                                  slot.kv_utilization = extractJsonNumber(kvJson, "utilization");
                                  slot.kv_cache_efficiency = extractJsonNumber(kvJson, "cache_efficiency");

                                  // Calculate tokens_cached from the KV cache position range
                                  // pos_max represents the highest position used, so total tokens = pos_max + 1
                                  // (positions are 0-indexed, so pos 0 = 1 token)
                                  if (slot.kv_pos_max >= 0) {
                                      slot.tokens_cached = slot.kv_pos_max + 1;
                                  } else {
                                      // Fallback to cells_used if pos_max is invalid
                                      slot.tokens_cached = slot.kv_cells_used;
                                  }
                              }
                          } else {
                              // Fallback: no kv_cache object, set defaults
                              slot.kv_pos_min = -1;
                              slot.kv_pos_max = -1;
                              slot.kv_cells_used = 0;
                              slot.kv_utilization = 0.0;
                              slot.kv_cache_efficiency = 0.0;
                              slot.tokens_cached = 0;
                          }

                          // Parse performance object if present
                          size_t perfPos = slotJson.find("\"performance\":");
                          if (perfPos != std::string::npos) {
                              // Check if performance is null
                              size_t nullPos = slotJson.find("null", perfPos + 14);
                              size_t objPos = slotJson.find("{", perfPos + 14);

                              if (objPos != std::string::npos && (nullPos == std::string::npos || objPos < nullPos)) {
                                  // Extract performance nested object
                                  size_t perfStart = objPos;
                                  size_t perfEnd = slotJson.find("}", perfStart);
                                  if (perfEnd != std::string::npos) {
                                      std::string perfJson = slotJson.substr(perfStart, perfEnd - perfStart + 1);

                                      slot.prompt_tokens_per_sec = extractJsonNumber(perfJson, "prompt_tokens_per_sec");
                                      slot.generation_tokens_per_sec = extractJsonNumber(perfJson, "generation_tokens_per_sec");
                                      slot.speculative_acceptance_rate = extractJsonNumber(perfJson, "speculative_acceptance_rate");
                                      slot.draft_tokens_total = extractJsonInt(perfJson, "draft_tokens_total");
                                      slot.draft_tokens_accepted = extractJsonInt(perfJson, "draft_tokens_accepted");
                                  }
                              }
                          }

                          m.slots.push_back(slot);
                          slotPos = endPos;
                      }

                      if (std::getenv("VERBOSE")) {
                          std::cout << "[Llama] Parsed " << m.slots.size() << " slots" << std::endl;
                      }

                      // Poll Metrics endpoint for global stats
                      std::string metricsUrl = baseUrl + "/metrics?model=" + urlEncode(detectedId);
                      auto metricsRes = executeCurl(metricsUrl, 10);
                      if (metricsRes.first == 0) {
                          const std::string& s = metricsRes.second;
                          m.prompt_tokens_total = (long long)parseMetric(s, "llamacpp:prompt_tokens_total");
                          m.tokens_predicted_total = (long long)parseMetric(s, "llamacpp:tokens_predicted_total");
                          m.prompt_seconds_total = parseMetric(s, "llamacpp:prompt_seconds_total");
                          m.tokens_predicted_seconds_total = parseMetric(s, "llamacpp:tokens_predicted_seconds_total");
                          m.n_decode_total = (long long)parseMetric(s, "llamacpp:n_decode_total");
                          m.n_busy_slots_per_decode = parseMetric(s, "llamacpp:n_busy_slots_per_decode");
                          m.prompt_tokens_seconds = parseMetric(s, "llamacpp:prompt_tokens_seconds");
                          m.predicted_tokens_seconds = parseMetric(s, "llamacpp:predicted_tokens_seconds");
                          m.kv_cache_usage_ratio = parseMetric(s, "llamacpp:kv_cache_usage_ratio");
                          m.kv_cache_tokens = (long long)parseMetric(s, "llamacpp:kv_cache_tokens");
                          m.requests_processing = (int)parseMetric(s, "llamacpp:requests_processing");
                          m.requests_deferred = (int)parseMetric(s, "llamacpp:requests_deferred");
                          m.n_tokens_max = (int)parseMetric(s, "llamacpp:n_tokens_max");
                      }
                 
                      // Poll Props
                      std::string propsUrl = baseUrl + "/props?model=" + urlEncode(detectedId);
                      auto propsRes = executeCurl(propsUrl, 2); 
                      if (propsRes.first == 0) {
                          const std::string& ps = propsRes.second;
                          size_t aliasPos = ps.find("\"model_alias\":");
                          if (aliasPos != std::string::npos) {
                              size_t q1 = ps.find("\"", aliasPos + 14);
                              size_t q2 = ps.find("\"", q1 + 1);
                              if (q1 != std::string::npos && q2 != std::string::npos) {
                                  m.modelName = ps.substr(q1 + 1, q2 - q1 - 1);
                              }
                          }
                          size_t pathPos = ps.find("\"model_path\":");
                          if (pathPos != std::string::npos) {
                              size_t q1 = ps.find("\"", pathPos + 13);
                              size_t q2 = ps.find("\"", q1 + 1);
                              if (q1 != std::string::npos && q2 != std::string::npos) {
                                  m.modelPath = ps.substr(q1 + 1, q2 - q1 - 1);
                              }
                          }
                          size_t ctxPos = ps.find("\"n_ctx\":");
                          if (ctxPos != std::string::npos) {
                              size_t endDigit = ps.find_first_not_of("0123456789", ctxPos + 8);
                              if (endDigit != std::string::npos) {
                                  try { m.n_ctx = std::stoi(ps.substr(ctxPos + 8, endDigit - ctxPos - 8)); } catch(...) {}
                              }
                          }
                      }
                 }
                 // If slots check fails, we still keep the previous m (READY state preserved)
                 // because we returned early on model check failure, 
                 // and here we just don't update fields if slots/metrics fail.
             }
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_ = m;
    }
}
} // namespace temper
