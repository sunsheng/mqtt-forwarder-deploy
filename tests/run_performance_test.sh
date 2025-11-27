#!/bin/bash

set -e

echo "启动测试环境..."
cd "$(dirname "$0")"

# 启动基础服务
docker compose -f docker-compose.test.yml up -d mqtt-broker-upstream mqtt-broker-downstream mqtt-forwarder

# 等待服务健康检查通过
echo "等待服务启动..."
timeout 60 bash -c '
  while ! docker compose -f docker-compose.test.yml ps | grep -q "healthy.*healthy.*healthy"; do
    echo "等待服务健康检查..."
    sleep 2
  done
'

echo "服务状态:"
docker compose -f docker-compose.test.yml ps

# 运行单元测试
echo "运行单元测试..."
docker compose -f docker-compose.test.yml run --rm unit-test

# 运行性能测试
echo "运行性能测试..."
docker compose -f docker-compose.test.yml run --rm performance-test

# 清理环境
echo "清理测试环境..."
docker compose -f docker-compose.test.yml down -v

echo "测试完成!"
