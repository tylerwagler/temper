
# Temper
**High-Performance GPU Telemetry & Control**

Temper is a lightweight, C++ based tool designed for AI clusters. It replaces `nvidia-smi` and `nvtop` by exposing deep hardware telemetry via a blazing fast HTTP JSON API, while maintaining robust thermal management capabilities.

## Features
*   **🚀 HTTP Telemetry API**: Real-time JSON access to Clocks, PCIe, ECC, and Process data on port `3001`.
*   **⚡ Zero-Latency Loop**: Native C++ event loop updates fan curves and power limits in milliseconds.
*   **🐳 Docker First**: Built to run as a privileged container alongside your AI stack.
*   **🔐 Safety Limits**: Hard-coded thermal safety and power verification.

## HTTP API
Poll `http://localhost:3001/metrics` for instant hardware status.

```bash
curl -s http://localhost:3001/metrics | jq .
```
*See [API.md](API.md) for the full schema documentation.*

## Quick Command Reference

| Command | Description |
| :--- | :--- |
| `temper info json` | Dump all GPU stats as JSON to stdout. |
| `temper status` | Compact, human-readable status line (Temp/Fan/Power). |
| `temper fanctl` | Start the dynamic fan control loop (requires root). |
| `temper power set <W>` | Enforce power limit on specific or all GPUs. |

## Installation

### Docker (Recommended)
Included in the AI Stack:
```bash
docker compose up -d fan-manager
```

### Manual Build
```bash
git clone https://github.com/tylerwagler/temper
cd temper
make
sudo make install
```

## Usage Examples

**Monitor Fan Speeds:**
```bash
temper fan
# Device 0: 35%
# Device 1: 40%
```

**Set Dynamic Curves (Root):**
```bash
# Set curve: 50C->30%, 70C->60%, 80C->90%
sudo temper fanctl 50:30 70:60 80:90
```