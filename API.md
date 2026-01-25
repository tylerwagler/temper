# GPU Telemetry API Specification

**Base URL**: `http://fan-manager:3001` (Internal Docker Network)  
**Host URL**: `http://localhost:3001` (Mapped to Host)

## Endpoints

### `GET /metrics`

Returns a real-time snapshot of all detected NVIDIA GPUs.

**Response Header**: `Content-Type: application/json`

**Response Body Schema**:

```json
{
  "gpus": [
    {
      "index": 0,                      // GPU Index (int)
      "name": "NVIDIA GeForce RTX 3090", // Model Name (string)
      "serial": "1322520097993",       // Board Serial Number (string)
      "vbios": "90.04.4A.00.08",       // Video BIOS Version (string)
      "p_state": 0,                    // Performance State: P0 (Max) -> P15 (Min) (int)
      
      "temperature": 42,               // Core Temperature in Celsius (int)
      "fan_speed_percent": 30,         // Current Fan Speed % (int)
      "target_fan_percent": 30,        // Fan control target set by this tool (int)
      
      "power_usage_mw": 125000,        // Current Power Draw in Milliwatts (int)
      "power_limit_mw": 350000,        // Current Power Limit in Milliwatts (int)
      
      "utilization": {
        "gpu": 98,                     // Compute Utilization % (int)
        "memory": 45                   // Memory Controller Utilization % (int)
      },
      
      "memory": {
        "total": 25769803776,          // Total VRAM in Bytes (long long)
        "used": 124803776              // Used VRAM in Bytes (long long)
      },
      
      "clocks": {
        "graphics": 1800,              // Current Graphics Clock in MHz (int)
        "memory": 9500,                // Current Memory Clock in MHz (int)
        "sm": 1800,                    // Current SM Clock in MHz (int)
        "video": 1500,                 // Current Video Encoder Clock in MHz (int)
        "max_graphics": 2100,          // Max Boose Clock in MHz (int)
        "max_memory": 10000            // Max Memory Clock in MHz (int)
      },
      
      "pcie": {
        "tx_throughput_kbs": 1500,     // Transmit (Upload) Bandwidth in KB/s (int)
        "rx_throughput_kbs": 50000,    // Receive (Download) Bandwidth in KB/s (int)
        "gen": 4,                      // Current PCIe Generation (e.g. 3, 4) (int)
        "width": 16                    // Current PCIe Width (e.g. 1, 8, 16) (int)
      },
      
      "ecc": {
        "volatile_single": 0,          // Single-bit errors since boot (long long)
        "volatile_double": 0,          // Double-bit errors since boot (long long)
        "aggregate_single": 0,         // Lifetime single-bit errors (long long)
        "aggregate_double": 0          // Lifetime double-bit errors (long long)
      },
      
      "processes": [
        {
          "pid": 1344,                 // Process ID (int)
          "name": "python",            // Process Name (string, "Unknown" if not resolved)
          "used_memory": 4096000       // Memory used by this process in Bytes (long long)
        }
      ],
      
      "throttle_alert": "SW Thermal Slowdown" // Empty string if normal, else description of throttling (string)
    }
  ]
}
```

## Notes for Frontend Implementation
- **Polling Rate**: The C++ tool updates metrics every **100ms (10Hz)**. Polling faster than this will return cached data.
- **Units**:
    - Power is in **milliwatts** (mW). Divide by 1000 for Watts.
    - Throughput is in **kilobytes/sec** (KB/s).
    - Memory is in **bytes**.
- **Metrics Availability**:
    - `processes` array may be empty if running in a container without PID namespace sharing or if no compute/graphics processes are active.
    - `throttle_alert` should be displayed prominently (red warning) if not empty.
