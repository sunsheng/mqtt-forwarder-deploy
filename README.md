# MQTT Forwarder

高性能MQTT消息转发服务，实现应用系统与外部系统之间的消息格式转换和路由。

## 功能特性

- 🔄 双向MQTT消息转发
- 📝 JSON格式转换和包装
- 🔧 环境变量配置
- 🐳 Docker容器化部署
- 📊 结构化日志输出
- 🔒 非root用户运行

## 快速部署

### 使用Docker Compose（推荐）

```bash
# 下载配置文件
wget https://raw.githubusercontent.com/sunsheng/mqtt-forwarder-deploy/main/docker-compose.yml

# 修改环境变量配置
vim docker-compose.yml

# 启动服务
docker-compose up -d

# 查看日志
docker-compose logs -f
```

### 使用Docker直接运行

```bash
docker run -d \
  --name mqtt-forwarder \
  --restart unless-stopped \
  --network host \
  -e SOURCE_BROKER=192.168.4.112 \
  -e TARGET_BROKER=192.168.6.10 \
  ghcr.io/sunsheng/mqtt-forwarder-deploy/mqtt-forwarder:latest
```

## 配置说明

| 环境变量 | 默认值 | 说明 |
|---------|--------|------|
| `SOURCE_BROKER` | 192.168.4.112 | 应用端MQTT Broker地址 |
| `TARGET_BROKER` | 192.168.6.10 | 外部系统MQTT Broker地址 |
| `MQTT_PORT` | 1883 | MQTT端口 |
| `TOPIC_PROPERTY_EVENT` | /ge/web/# | 属性事件主题 |
| `TOPIC_COMMAND` | /gc/web/# | 指令主题 |

## 数据流向

```
应用系统 (SOURCE_BROKER) → 格式转换 → 外部系统 (TARGET_BROKER)
```

## 本地开发

```bash
# 克隆代码
git clone https://github.com/sunsheng/mqtt-forwarder-deploy.git
cd mqtt-forwarder-deploy

# 构建
mkdir build && cd build
cmake ..
make

# 运行
./modular/mqtt_forwarder_modular
```

## 依赖要求

- libmosquitto
- libcjson

## 许可证

MIT License