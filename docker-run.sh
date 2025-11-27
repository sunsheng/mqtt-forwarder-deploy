#!/bin/bash

# 构建镜像
docker build -t mqtt-forwarder:latest .

docker rm -f mqtt-forwarder

# 运行容器
docker run -d \
  --name mqtt-forwarder \
  --restart unless-stopped \
  --network host \
  mqtt-forwarder:latest

# 查看日志
docker logs mqtt-forwarder -f
