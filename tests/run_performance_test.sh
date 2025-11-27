#!/bin/bash

set -e

echo "启动测试环境..."
cd "$(dirname "$0")"

# 启动所有服务，depends_on 会自动管理启动顺序
echo "启动所有服务..."
docker compose -f docker-compose.test.yml up -d mqtt-forwarder

echo "等待服务启动完成..."
sleep 5

docker compose -f docker-compose.test.yml logs --tail=15 mqtt-forwarder

echo "最终服务状态:"
docker compose -f docker-compose.test.yml ps

echo "运行性能测试..."
docker compose -f docker-compose.test.yml run --rm performance-test

echo "清理测试环境..."
docker compose -f docker-compose.test.yml down
