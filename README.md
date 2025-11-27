# MQTT Forwarder

高性能MQTT消息转发服务，实现上下游系统之间的消息格式转换和路由。

## 功能特性

- 🔄 双向MQTT消息转发
- 📝 JSON格式转换和包装
- 🔧 JSON配置文件支持
- 🐳 Docker容器化部署
- 📊 结构化日志输出
- 🔒 非root用户运行

## 快速部署

### 使用Docker Compose（推荐）

```bash
# 下载配置文件
wget https://raw.githubusercontent.com/sunsheng/mqtt-forwarder-deploy/main/docker-compose.yml
wget https://raw.githubusercontent.com/sunsheng/mqtt-forwarder-deploy/main/config.json

# 修改配置文件
vim config.json

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
  -v $(pwd)/config.json:/etc/mqtt-forwarder.json \
  -e LOG_LEVEL=DEBUG \
  ghcr.io/sunsheng/mqtt-forwarder-deploy/mqtt-forwarder:latest
```

## 配置说明

### JSON配置文件 (config.json)

```json
{
  "log_level": "info",
  "mqtt": {
    "port": 1883,
    "keepalive": 60,
    "qos": 0,
    "retain": false,
    "clean_session": true,
    "username": null,
    "password": null
  },
  "clients": [
    {
      "name": "upstream",
      "ip": "192.168.4.112",
      "port": null,
      "client_id": "mqtt_forwarder_upstream"
    },
    {
      "name": "downstream",
      "ip": "192.168.6.10",
      "port": null,
      "client_id": "mqtt_forwarder_downstream"
    }
  ],
  "rules": [
    {
      "name": "property_events",
      "description": "属性事件转发：下游->上游",
      "source": {
        "client": "downstream",
        "topic": "/ge/web/#"
      },
      "target": {
        "client": "upstream",
        "topic": "/ge/web/#"
      },
      "callback": "EventCall",
      "enabled": true
    }
  ]
}
```

### 环境变量

| 环境变量 | 说明 | 默认值 |
|---------|------|--------|
| `LOG_LEVEL` | 日志级别（debug/info/error），优先级高于JSON配置 | info |

### 配置优先级

**日志级别**: 环境变量 > JSON配置 > 默认值

## 使用方法

### 命令行参数

```bash
# 使用默认配置文件 (./config.json 或 /etc/mqtt-forwarder.json)
./mqtt_forwarder

# 指定配置文件
./mqtt_forwarder -c /path/to/config.json
./mqtt_forwarder --config=config.json

# 查看帮助
./mqtt_forwarder -h
```

## 数据流向

### 属性事件转发 (Property Events)
```
下游系统 (/ge/web/#) → JSON格式转换 → 上游系统 (/ge/web/#)
DOWNSTREAM_BROKER:1883     包装处理      UPSTREAM_BROKER:1883
```

### 指令转发 (Commands)  
```
上游系统 (/gc/web/#) → JSON格式解析 → 下游系统 (/gc/web/#)
UPSTREAM_BROKER:1883     格式转换      DOWNSTREAM_BROKER:1883
```

### 消息处理流程
1. **属性事件**: 下游系统发布设备状态/属性变化 → 转发器接收并转换格式 → 推送到上游系统
2. **控制指令**: 上游系统发布控制命令 → 转发器接收并解析 → 转发到下游系统执行
3. **格式转换**: 自动处理JSON消息的包装和解包，确保两端系统兼容

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
./mqtt_forwarder -c ../config.json
```

## 依赖要求

- libmosquitto
- libcjson

## 许可证

MIT License