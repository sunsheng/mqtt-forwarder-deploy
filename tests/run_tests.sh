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

# 测试无效配置
echo "Testing invalid configuration..."
/usr/local/bin/mqtt_forwarder -c /tests/invalid_config.json --validate-only
if [ $? -ne 0 ]; then
    echo "✓ Invalid configuration test passed"
else
    echo "✗ Invalid configuration test failed"
    exit 1
fi

echo "All tests passed!"
