# MQTT Forwarder Suite 项目结构

## 目录结构

```
mqtt-forwarder-deploy/
├── CMakeLists.txt          # 根CMake配置
├── common/                 # 通用头文件
│   ├── logger.h           # 日志系统
│   ├── mqtt_forwarder.h   # 主API定义
│   └── callbacks.h        # 回调函数声明
├── modular/               # 模块化版本
│   ├── CMakeLists.txt     # 子项目配置
│   ├── mqtt_main.c        # 主程序
│   ├── forwarder_engine.c # 转发引擎
│   └── message_handlers.c # 消息处理器
├── legacy/                # 传统单文件版本
│   ├── CMakeLists.txt     # 子项目配置
│   └── mqtt_forwarder.c   # 单文件实现
├── paho/                  # Paho库版本
│   ├── CMakeLists.txt     # 子项目配置
│   └── mqtt_forwarder_paho.c # Paho实现
└── build/                 # 构建输出
    ├── modular/mqtt_forwarder_modular
    ├── legacy/mqtt_forwarder
    └── paho/mqtt_forwarder_paho
```

## 构建命令

```bash
# 构建所有版本
mkdir build && cd build
cmake ..
make

# 构建特定版本
make mqtt_forwarder_modular  # 模块化版本
make mqtt_forwarder          # 传统版本
make mqtt_forwarder_paho     # Paho版本
```

## 版本说明

- **modular**: 最新的模块化架构，支持多转发规则，线程安全
- **legacy**: 传统单文件版本，简单直接
- **paho**: 使用Paho异步库的版本，自动重连能力强
