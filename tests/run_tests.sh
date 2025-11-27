#!/bin/bash

echo "Running MQTT Forwarder Tests..."

# 测试配置文件解析
echo "Testing configuration parsing..."
/usr/local/bin/mqtt_forwarder -c /tests/test_config.json --validate-only
if [ $? -eq 0 ]; then
    echo "✓ Configuration parsing test passed"
else
    echo "✗ Configuration parsing test failed"
    exit 1
fi

# 测试无效配置 - 端口超出范围
echo "Testing invalid port configuration..."
/usr/local/bin/mqtt_forwarder -c /tests/invalid_config.json --validate-only
if [ $? -ne 0 ]; then
    echo "✓ Invalid port configuration test passed"
else
    echo "✗ Invalid port configuration test failed"
    exit 1
fi

# 测试无效配置 - 缺少规则
echo "Testing missing rules configuration..."
/usr/local/bin/mqtt_forwarder -c /tests/no_rules_config.json --validate-only
if [ $? -ne 0 ]; then
    echo "✓ Missing rules configuration test passed"
else
    echo "✗ Missing rules configuration test failed"
    exit 1
fi

# 测试无效配置 - 无效JSON
echo "Testing invalid JSON configuration..."
/usr/local/bin/mqtt_forwarder -c /tests/invalid_json_config.json --validate-only
if [ $? -ne 0 ]; then
    echo "✓ Invalid JSON configuration test passed"
else
    echo "✗ Invalid JSON configuration test failed"
    exit 1
fi

echo "All tests passed!"
