# MQTT Benchmark 分析报告

## 1) 概要

概述：此报告分析了 tests/mqtt_benchmark.py 脚本及其与 tests/docker-compose.test.yml 和 tests/perf_config.json 的集成方式。mqtt_benchmark.py 的目的是对 mqtt-forwarder 进行基准测试：一次性生成大量 JSON 数组消息到文件，然后在 mqtt-broker-downstream 容器中使用 mosquitto_pub -l 批量发布这些消息，并在 mqtt-broker-upstream 容器中使用 mosquitto_sub 订阅接收，统计发送/接收、耗时、吞吐和丢包率。

集成方式：tests/docker-compose.test.yml 定义了两个独立的 Mosquitto broker（mqtt-broker-upstream 与 mqtt-broker-downstream），并通过 healthcheck 进行就绪检测；mqtt-forwarder 在容器内通过挂载的配置文件 tests/perf_config.json 连接到这些 broker。perf_config.json 中 clients.ip 字段使用容器服务名（mqtt-broker-upstream / mqtt-broker-downstream），因此 mqtt-forwarder 在同一网络中可以通过服务名解析到对应 broker。脚本通过 docker compose exec 在各自 broker 容器中运行 mosquitto_pub/mosquitto_sub，并在容器内使用 -h localhost 连接本容器的 broker，设计上是相互匹配的。

## 2) 主要发现

- docker-compose.test.yml 为两个 broker（mqtt-broker-upstream 和 mqtt-broker-downstream）定义了 healthcheck；mqtt-forwarder 使用 depends_on: service_healthy 来依赖这两个服务的就绪状态。
- perf_config.json 在 clients.ip 中使用了容器服务名（mqtt-broker-upstream / mqtt-broker-downstream），与 docker-compose 服务名一致，mqtt-forwarder 可解析并连接这些服务名。
- mqtt_benchmark.py 使用 docker compose exec 在 broker 容器内运行 mosquitto_pub / mosquitto_sub，并在容器内通过 -h localhost 连接本容器的 broker（与 exec 行为一致）。
- tests/mosquitto.conf 允许匿名连接（listener 1883，allow_anonymous true），支持脚本在容器内用 localhost 直接连接 broker。
- tests/run_tests.sh 使用 /usr/local/bin/mqtt_forwarder --validate-only 对若干配置文件进行验证，覆盖了正常配置与多种无效配置场景。

## 3) 风险与问题

1. 健康就绪竞态（高）：脚本在 docker compose up 后仅使用固定的 sleep(5) 等待，并在启动接收线程后使用 sleep(2) 等待接收端就绪。固定等待存在竞态，可能在健康检查尚未通过时就开始发送或在接收端尚未就绪时发送，导致测量偏差或错误。建议改为轮询容器 health 状态或由接收线程显式通知准备完成。

2. 子进程错误处理不足（高）：发送与接收使用的 subprocess 未能可靠地捕获并处理 stderr/返回码，接收端也没有在子进程结束后检查 returncode，可能掩盖启动或连接错误。需要在失败时记录详细 stderr 并在必要时重试或报错退出。

3. all_messages.txt 冲突（中）：脚本使用固定文件名 all_messages.txt 存放消息，导致并发运行或多次运行时可能出现冲突或污染仓库。建议使用 tempfile.NamedTemporaryFile 或为每个场景生成独立文件并在结束时删除。

4. 发送计数准确性（中）：脚本仅在 mosquitto_pub 返回码为 0 时将 sent_count 置为 message_count，无法检测部分发送成功的情况。建议改为逐条发布并记录成功/失败（或使用可返回结果的发布 API）。

5. QoS 固定为 0（中）：脚本和配置文件默认使用 QoS=0，适合吞吐测试但不适用于可靠性评估。若需可靠性测试，应支持 QoS=1/2 并记录确认信息与重试行为。

6. 守护线程与 join 行为（低）：接收线程被设置为 daemon=True，同时又对其进行 join(timeout)，这可能导致主进程退出时线程被强制终止，影响日志收集或清理。建议改为非守护线程并显式管理子进程生命周期。

7. 异常路径未必执行 docker compose down（高）：main() 中在 try/finally 里只清理了消息文件，docker compose down 在 try 之后，若运行中发生异常可能跳过 down。建议将 docker compose down 放入 finally，以保证在任何异常路径都能执行清理。

## 4) 建议的改进

- 用容器健康轮询替代固定 sleep：

提供一个 Python 函数示例用于等待 docker 容器健康：

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

- 在接收端使用 threading.Event 通知就绪并替换 sleep(2)：
  - 在 start_receiver 中创建 ready_event 并在确认 mosquitto_sub 启动并能读取 stdout 后调用 ready_event.set()。
  - 在 run_benchmark 里在发送前调用 ready_event.wait(timeout=...)。

- 使用临时文件或为每个场景生成独立消息文件，并在 cleanup 中删除，避免 all_messages.txt 冲突。

- 改进 subprocess 错误处理：使用 subprocess.Popen 并在结束时调用 communicate() 获取 stderr，检查 returncode 并记录详细错误消息；在发送失败时考虑重试或标记失败场景。

- 可选（大改动）：使用 paho-mqtt 从 Python 直接发布/订阅，这样可以精确记录每条消息的发送与接收时间，支持 QoS 控制并便于统计延迟分布（P50/P95/P99）。

- 在 main() 的异常处理里保证 docker compose down 在 finally 中执行，避免遗留容器或网络。

## 5) 检查清单

- 已确认 docker-compose.test.yml 对 broker 服务定义了 healthcheck，且 mqtt-forwarder 使用 depends_on 指向 service_healthy。
- 已确认 perf_config.json 中 clients.ip 使用了容器服务名（mqtt-broker-upstream / mqtt-broker-downstream）。
- 已确认 tests/mosquitto.conf 允许匿名连接并监听 1883（listener 1883; allow_anonymous true）。
- 已确认 tests/run_tests.sh 中对配置验证的调用逻辑（--validate-only 检查正常与无效配置）。

## 6) Recommendation / next steps

- 优先实现 wait_for_health 以替换固定 sleep，并在 start_receiver 中使用 ready_event 来确保接收端就绪。可选地将发布/订阅替换为 paho-mqtt 以获得更精确的测量与控制。

## 7) 参考链接

- tests/mqtt_benchmark.py: https://github.com/sunsheng/mqtt-forwarder-deploy/blob/main/tests/mqtt_benchmark.py
- tests/docker-compose.test.yml: https://github.com/sunsheng/mqtt-forwarder-deploy/blob/main/tests/docker-compose.test.yml
- tests/perf_config.json: https://github.com/sunsheng/mqtt-forwarder-deploy/blob/main/tests/perf_config.json
- tests/mosquitto.conf: https://github.com/sunsheng/mqtt-forwarder-deploy/blob/main/tests/mosquitto.conf
- tests/run_tests.sh: https://github.com/sunsheng/mqtt-forwarder-deploy/blob/main/tests/run_tests.sh
