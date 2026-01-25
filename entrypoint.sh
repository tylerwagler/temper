#!/bin/bash
set -e

# Default setpoints if not provided
FAN_SETPOINTS=${FAN_SETPOINTS:-"50:30 70:60 80:95"}

if [ -n "$POWER_LIMIT" ]; then
    echo "Setting GPU power limit to ${POWER_LIMIT}W..."
    /app/temper power set "$POWER_LIMIT"
fi

echo "Starting nvml-tool with fanctl setpoints: $FAN_SETPOINTS"

# Exec the nvml-tool fanctl command
exec /app/temper fanctl $FAN_SETPOINTS
