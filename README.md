# MQTT Forwarder

MQTT message forwarding service with format conversion between application and external systems.

## Quick Start

```bash
# Build
mkdir build && cd build
cmake ..
make

# Run
./modular/mqtt_forwarder_modular
```

## Docker

```bash
# Build and run with Docker Compose
docker-compose up --build
```

## Configuration

Configure via environment variables:

- `SOURCE_BROKER` - Application MQTT Broker (default: 192.168.4.112)
- `TARGET_BROKER` - External system MQTT Broker (default: 192.168.6.10)
- `MQTT_PORT` - MQTT port (default: 1883)
- `TOPIC_PROPERTY_EVENT` - Property/event topic (default: /ge/web/#)
- `TOPIC_COMMAND` - Command topic (default: /gc/web/#)

## Data Flow

Application (SOURCE_BROKER) → Format Conversion → External System (TARGET_BROKER)

## Dependencies

- libmosquitto
- libcjson