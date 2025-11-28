# MQTT Benchmark 分析报告

## 1. 概要

`mqtt_benchmark.py` 是一个 MQTT 性能基准测试脚本，用于评估 MQTT 消息转发器在不同负载场景下的性能表现。该脚本通过预生成大量动态 JSON 消息，然后使用 Docker Compose 环境中的 Mosquitto 工具进行发布和订阅测试，最终统计吞吐量、丢失率等关键性能指标。

该脚本与 `docker-compose.test.yml` 紧密集成，后者定义了完整的测试环境，包括两个 Mosquitto 代理（上游和下游）以及 mqtt-forwarder 服务。`perf_config.json` 配置文件为 mqtt-forwarder 提供运行时配置，定义了客户端连接信息和消息转发规则，确保测试消息能够从下游代理正确转发到上游代理。

## 2. 主要发现

- docker-compose.test.yml 定义了两个 Mosquitto 代理服务（mqtt-broker-upstream 和 mqtt-broker-downstream），均配置了健康检查；mqtt-forwarder 服务使用 depends_on 的 service_healthy 条件确保在代理就绪后启动。
- perf_config.json 中 clients.ip 字段使用容器服务名称（mqtt-broker-upstream / mqtt-broker-downstream），与 compose 文件中的服务名称匹配。
- mqtt_benchmark.py 使用 docker compose exec 在代理容器内运行 mosquitto_pub / mosquitto_sub 命令，并使用 -h localhost 连接本地代理（与 exec 方式一致）。
- mosquitto.conf 配置允许匿名连接（listener 1883, allow_anonymous true）。
- run_tests.sh 使用 --validate-only 参数对多个测试 JSON 配置文件进行配置验证测试。

## 3. 风险与问题

1. **健康就绪竞态条件** (高优先级): 脚本在 docker compose up 后使用固定的 sleep(5) 等待，在发送前使用 sleep(2) 等待接收器就绪；应该改为轮询容器健康状态或等待接收器就绪信号。

2. **子进程错误处理不完善** (高优先级): send/receive 子进程没有可靠地捕获和处理 stderr/returncode，可能会隐藏失败情况。

3. **docker compose down 异常处理** (高优先级): main() 函数在某些异常情况下可能跳过 docker compose down 清理操作；应将清理逻辑放在 finally 块中。

4. **all_messages.txt 文件冲突** (中优先级): 使用固定文件名可能导致并发冲突；建议使用 tempfile 或按场景生成独立文件。

5. **sent_count 准确性问题** (中优先级): 无法检测部分发送失败的情况。

6. **QoS 固定为 0** (中优先级): 可能不适合可靠性测试场景。

7. **daemon 线程 join 行为** (低优先级): 接收线程标记为 daemon=True 但仍然调用 join；建议使用非 daemon 线程并显式清理进程。

## 4. 建议的改进

- **用健康检查轮询替代固定等待时间**。提供一个 Python 函数片段，使用 docker inspect 等待容器健康状态，并展示如何为 mqtt-broker-upstream、mqtt-broker-downstream 和 mqtt-forwarder 调用：

```python
import subprocess, time

def wait_for_health(container_name, timeout=60):
    start = time.time()
    while time.time() - start < timeout:
        try:
            out = subprocess.check_output([
                'docker', 'inspect', '--format={{.State.Health.Status}}', container_name
            ], text=True).strip()
        except subprocess.CalledProcessError:
            out = ''
        if out == 'healthy':
            return True
        time.sleep(1)
    return False

# usage after `docker compose up -d`:
# wait_for_health('mqtt-broker-upstream', 60)
# wait_for_health('mqtt-broker-downstream', 60)
# wait_for_health('mqtt-forwarder', 60)
```

- **在 start_receiver 中使用 threading.Event 信号就绪状态**，替代 sleep(2)。提供简短伪代码，在接收器成功开始读取 stdout 时设置 ready_event。

- **使用 tempfile 生成每次运行的消息文件**，或生成按场景命名的文件并在清理时删除。

- **改进子进程处理**: 使用 Popen 和 communicate() 方法，检查 returncode 并在失败时记录 stderr。

- **考虑使用 paho-mqtt 从 Python 直接发布/订阅**，以获得更精确的指标（可选的较大改动）。

- **确保 docker compose down 在 finally 块中执行**。

## 5. 检查清单

- 已确认 docker-compose.test.yml 包含代理的健康检查配置，且 mqtt-forwarder 的 depends_on 使用了 service_healthy 条件。
- 已确认 perf_config.json 的 client ip 字段使用容器服务名称。
- 已确认 mosquitto.conf 允许匿名连接并监听 1883 端口。
- 已确认 run_tests.sh 的验证检查和行为。

## 6. 建议 / 下一步

建议首先实现 wait_for_health 改进和接收器 ready_event 功能。可选地，使用 paho-mqtt 替代 docker exec pub/sub 以获得更丰富的指标。

## 7. 参考链接

- tests/mqtt_benchmark.py: https://github.com/sunsheng/mqtt-forwarder-deploy/blob/main/tests/mqtt_benchmark.py
- tests/docker-compose.test.yml: https://github.com/sunsheng/mqtt-forwarder-deploy/blob/main/tests/docker-compose.test.yml
- tests/perf_config.json: https://github.com/sunsheng/mqtt-forwarder-deploy/blob/main/tests/perf_config.json
- tests/mosquitto.conf: https://github.com/sunsheng/mqtt-forwarder-deploy/blob/main/tests/mosquitto.conf
- tests/run_tests.sh: https://github.com/sunsheng/mqtt-forwarder-deploy/blob/main/tests/run_tests.sh
