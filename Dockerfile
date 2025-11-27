# 构建阶段
FROM debian:12-slim AS build

# 安装构建依赖（单独层，便于缓存）
RUN apt-get update && apt-get install -y \
    gcc \
    cmake \
    ninja-build \
    pkg-config \
    libmosquitto-dev \
    libcjson-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# 先复制构建配置文件（变化较少，利于缓存）
COPY CMakeLists.txt ./

# 配置构建环境（缓存cmake配置）
RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# 复制源代码（变化较频繁，放在后面）
COPY src/ ./src/

# 构建项目
RUN cmake --build build --target mqtt_forwarder

# 运行阶段 - 精简镜像
FROM debian:12-slim AS runtime

# 安装运行时依赖
RUN apt-get update && apt-get install -y \
    libmosquitto1 \
    libcjson1 \
    tzdata \
    && ln -sf /usr/share/zoneinfo/Asia/Shanghai /etc/localtime \
    && echo "Asia/Shanghai" > /etc/timezone \
    && rm -rf /var/lib/apt/lists/*

# 复制可执行文件
COPY --from=build /src/build/mqtt_forwarder /usr/local/bin/

# 创建非root用户
RUN useradd -r -s /bin/false mqtt-forwarder

USER mqtt-forwarder

CMD ["/usr/local/bin/mqtt_forwarder"]
