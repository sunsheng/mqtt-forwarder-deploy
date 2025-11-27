#!/bin/bash

set -e

echo "启动测试环境..."
cd "$(dirname "$0")"

# 启动基础服务
docker compose -f docker-compose.test.yml up -d mqtt-broker-upstream mqtt-broker-downstream mqtt-forwarder

# 等待服务健康检查通过 - 详细状态显示
echo "等待服务启动..."
timeout 180 bash -c '
  while true; do
    echo "检查服务状态..."
    
    # 显示详细服务状态
    echo "=== 服务状态详情 ==="
    docker compose -f docker-compose.test.yml ps
    
    # 检查健康状态
    status=$(docker compose -f docker-compose.test.yml ps --format "table {{.Service}}\t{{.Status}}" | grep -c "healthy" || echo "0")
    
    if [ "$status" -eq 3 ]; then
      echo "所有服务已就绪"
      break
    fi
    
    echo "等待更多服务就绪... ($status/3)"
    
    # 显示未就绪服务的日志
    echo "=== 检查未就绪服务日志 ==="
    for service in mqtt-broker-upstream mqtt-broker-downstream mqtt-forwarder; do
      service_status=$(docker compose -f docker-compose.test.yml ps $service --format "table {{.Status}}" | tail -n +2)
      if [[ ! "$service_status" =~ "healthy" ]]; then
        echo "--- $service 状态: $service_status ---"
        echo "最近日志:"
        docker compose -f docker-compose.test.yml logs --tail=15 $service
      fi
    done
    
    sleep 8
  done
'

echo "最终服务状态:"
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
