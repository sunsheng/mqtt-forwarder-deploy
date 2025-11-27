# 构建阶段
FROM debian:12-slim AS build

# 安装构建依赖
RUN apt-get update && apt-get install -y \
    gcc \
    cmake \
    ninja-build \
    libmosquitto-dev \
    libcjson-dev \
    && rm -rf /var/lib/apt/lists/*

# 复制源代码
WORKDIR /src
COPY . .

# 构建项目 - Release版本
RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --target mqtt_forwarder_modular

# 运行阶段
FROM debian:12-slim

# 安装运行时依赖和设置时区
RUN apt-get update && apt-get install -y \
    libmosquitto1 \
    libcjson1 \
    tzdata \
    && ln -sf /usr/share/zoneinfo/Asia/Shanghai /etc/localtime \
    && echo "Asia/Shanghai" > /etc/timezone \
    && rm -rf /var/lib/apt/lists/*

# 复制可执行文件
COPY --from=build /src/build/modular/mqtt_forwarder_modular /usr/local/bin/

# 创建非root用户
RUN useradd -r -s /bin/false mqtt-forwarder

# 切换用户
USER mqtt-forwarder

# 启动命令
CMD ["/usr/local/bin/mqtt_forwarder_modular"]
