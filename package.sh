#!/bin/bash

# 获取当前目录名作为压缩包名
DIR_NAME=$(basename "$PWD")
ARCHIVE_NAME="${DIR_NAME}.tar.gz"

# 创建临时目录
TEMP_DIR="/tmp/${DIR_NAME}"
mkdir -p "$TEMP_DIR"

# 使用rsync复制整个目录结构，排除隐藏文件和build目录
rsync -av  --exclude='build' . "$TEMP_DIR/"

# 打包临时目录
cd /tmp && tar -czf "$ARCHIVE_NAME" "${DIR_NAME}"

# 移动压缩包到当前目录
mv "$ARCHIVE_NAME" "$OLDPWD/"

# 清理临时目录
rm -rf "$TEMP_DIR"

echo "打包完成: $ARCHIVE_NAME"
