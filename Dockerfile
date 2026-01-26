# Build stage
FROM nvidia/cuda:13.1.1-devel-ubuntu24.04 AS builder

WORKDIR /usr/src/nvml-tool
COPY . .

# Manual paths for NVML if pkg-config fails
RUN make NVML_CFLAGS="-I/usr/local/cuda/targets/x86_64-linux/include" NVML_LIBS="-lnvidia-ml"

# Runtime stage
FROM nvidia/cuda:13.1.1-base-ubuntu24.04

RUN apt-get update && apt-get install -y ipmitool sshpass openssh-client && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /usr/src/nvml-tool/build/temper /app/temper

# Entrypoint to handle environment variable based configuration
COPY entrypoint.sh /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

# Expose metric port
EXPOSE 3001

ENTRYPOINT ["/app/entrypoint.sh"]
