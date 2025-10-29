# MQTT Forwarder

MQTT message forwarding service with multiple implementation variants.

## Quick Start

```bash
# Build all versions
mkdir build && cd build
cmake ..
make

# Run modular version (recommended)
./modular/mqtt_forwarder_modular
```

## Versions

- **modular/**: Modular architecture with multi-rule forwarding and thread safety
- **legacy/**: Single-file traditional implementation  
- **paho/**: Paho async library version with auto-reconnect

## Dependencies

- libmosquitto
- libcjson
- paho-mqtt3a (for paho version)

## Docker

```bash
# Build and run with Docker Compose
docker-compose up --build

# Or use the run script
./docker-run.sh
```

## Build Options

```bash
# Build specific version only
cmake -DBUILD_MODULAR=ON -DBUILD_LEGACY=OFF -DBUILD_PAHO=OFF ..
```