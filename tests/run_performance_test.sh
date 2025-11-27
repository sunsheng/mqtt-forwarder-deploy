#!/bin/bash

set -e

echo "启动测试环境..."
cd "$(dirname "$0")"

# 按顺序启动服务
echo "启动 MQTT brokers..."
docker compose -f docker-compose.test.yml up -d mqtt-broker-upstream mqtt-broker-downstream

echo "等待 MQTT brokers 启动..."
sleep 20

echo "启动 mqtt-forwarder..."
docker compose -f docker-compose.test.yml up -d mqtt-forwarder

echo "等待 mqtt-forwarder 启动..."
sleep 5

docker compose -f docker-compose.test.yml logs --tail=30 mqtt-forwarder

echo "最终服务状态:"
docker compose -f docker-compose.test.yml ps

echo "运行性能测试..."
docker compose -f docker-compose.test.yml run --rm performance-test

echo "清理测试环境..."
docker compose -f docker-compose.test.yml down
