#!/usr/bin/env python3
import paho.mqtt.client as mqtt
import json
import time
import threading
import statistics
import os
import socket

def check_broker_connection(host, port, timeout=5):
    """检查MQTT broker是否可连接"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        result = sock.connect_ex((host, port))
        sock.close()
        return result == 0
    except:
        return False

class PerformanceTest:
    def run_throughput_test(self, message_count=1000, message_size=100):
        """测试吞吐量"""
        print(f"开始吞吐量测试: {message_count} 条消息, 每条 {message_size} 字节")
        
        # 从环境变量获取broker地址
        upstream_broker = os.getenv('UPSTREAM_BROKER', '127.0.0.1')
        downstream_broker = os.getenv('DOWNSTREAM_BROKER', '127.0.0.1')
        upstream_port = 1883 if upstream_broker != '127.0.0.1' else 1884
        downstream_port = 1883 if downstream_broker != '127.0.0.1' else 1885
        
        # 独立的测试状态
        received_count = 0
        sent_count = 0
        lock = threading.Lock()
        stop_event = threading.Event()
        
        def on_connect(client, userdata, flags, reason_code, properties):
            print(f"Receiver connected: {reason_code}")
            client.subscribe("/test/perf/#")
            
        def on_message(client, userdata, msg):
            nonlocal received_count
            with lock:
                received_count += 1
        
        # 接收线程
        def receiver_thread():
            try:
                receiver = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, "perf_receiver")
                receiver.on_connect = on_connect
                receiver.on_message = on_message
                receiver.connect(upstream_broker, upstream_port, 60)
                
                while not stop_event.is_set():
                    receiver.loop(timeout=0.1)
                receiver.disconnect()
            except Exception as e:
                print(f"Receiver error: {e}")
        
        # 启动接收线程
        receiver_t = threading.Thread(target=receiver_thread)
        receiver_t.start()
        
        time.sleep(2)  # 等待连接建立
        
        # 发送端
        try:
            sender = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, "perf_sender")
            sender.connect(downstream_broker, downstream_port, 60)
            
            # 生成测试数据
            test_data = {"data": "x" * message_size}
            payload = json.dumps(test_data)
            
            # 开始测试
            start_time = time.time()
            
            for i in range(message_count):
                topic = f"/test/perf/device_{i % 10}"
                sender.publish(topic, payload)
                sent_count += 1
                time.sleep(0.001)  # 控制发送速率
                
            # 等待接收完成
            timeout = 30
            while received_count < message_count and timeout > 0:
                time.sleep(0.1)
                timeout -= 0.1
                
            end_time = time.time()
            duration = end_time - start_time
            
            sender.disconnect()
            
        except Exception as e:
            print(f"Sender error: {e}")
            duration = 1
            
        # 停止接收线程
        stop_event.set()
        receiver_t.join(timeout=5)
        
        return {
            'sent': sent_count,
            'received': received_count,
            'duration': duration,
            'throughput': received_count / duration if duration > 0 else 0,
            'loss_rate': (sent_count - received_count) / sent_count * 100 if sent_count > 0 else 0
        }
    
    def run_latency_test(self, sample_count=100):
        """测试延迟"""
        print(f"开始延迟测试: {sample_count} 个样本")
        
        # 从环境变量获取broker地址
        upstream_broker = os.getenv('UPSTREAM_BROKER', '127.0.0.1')
        downstream_broker = os.getenv('DOWNSTREAM_BROKER', '127.0.0.1')
        upstream_port = 1883 if upstream_broker != '127.0.0.1' else 1884
        downstream_port = 1883 if downstream_broker != '127.0.0.1' else 1885
        
        # 独立的测试状态
        received_count = 0
        latencies = []
        pending_messages = {}
        lock = threading.Lock()
        stop_event = threading.Event()
        
        def on_connect(client, userdata, flags, reason_code, properties):
            print(f"Latency receiver connected: {reason_code}")
            client.subscribe("/test/perf/#")
            
        def on_message(client, userdata, msg):
            nonlocal received_count
            try:
                payload = json.loads(msg.payload.decode())
                msg_id = payload.get('msg_id')
                
                with lock:
                    if msg_id in pending_messages:
                        receive_time = time.time()
                        send_time = pending_messages[msg_id]
                        latency = (receive_time - send_time) * 1000  # 转换为毫秒
                        latencies.append(latency)
                        del pending_messages[msg_id]
                    received_count += 1
                    
            except (json.JSONDecodeError, KeyError):
                # 只捕获JSON相关异常
                pass
        
        # 接收线程
        def receiver_thread():
            try:
                receiver = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, "latency_receiver")
                receiver.on_connect = on_connect
                receiver.on_message = on_message
                receiver.connect(upstream_broker, upstream_port, 60)
                
                while not stop_event.is_set():
                    receiver.loop(timeout=0.1)
                receiver.disconnect()
            except Exception as e:
                print(f"Latency receiver error: {e}")
        
        # 启动接收线程
        receiver_t = threading.Thread(target=receiver_thread)
        receiver_t.start()
        
        time.sleep(2)  # 等待连接建立
        
        # 发送端
        try:
            sender = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, "latency_sender")
            sender.connect(downstream_broker, downstream_port, 60)
            
            for i in range(sample_count):
                msg_id = f"latency_{i}"
                send_time = time.time()
                
                with lock:
                    pending_messages[msg_id] = send_time
                
                payload = json.dumps({"msg_id": msg_id, "test": i})
                sender.publish(f"/test/perf/{msg_id}", payload)
                
                time.sleep(0.01)  # 控制发送速率
                
            # 等待所有消息接收完成
            timeout = 10
            while len(pending_messages) > 0 and timeout > 0:
                time.sleep(0.1)
                timeout -= 0.1
                
            sender.disconnect()
            
        except Exception as e:
            print(f"Latency sender error: {e}")
            
        # 停止接收线程
        stop_event.set()
        receiver_t.join(timeout=5)
        
        if latencies:
            return {
                'samples': len(latencies),
                'avg_latency_ms': statistics.mean(latencies),
                'min_latency_ms': min(latencies),
                'max_latency_ms': max(latencies),
                'p95_latency_ms': statistics.quantiles(latencies, n=20)[18] if len(latencies) > 20 else max(latencies),
                'p99_latency_ms': statistics.quantiles(latencies, n=100)[98] if len(latencies) > 100 else max(latencies)
            }
        return None

def main():
    os.makedirs('results', exist_ok=True)
    
    # 从环境变量获取broker地址
    upstream_broker = os.getenv('UPSTREAM_BROKER', '127.0.0.1')
    downstream_broker = os.getenv('DOWNSTREAM_BROKER', '127.0.0.1')
    upstream_port = 1883 if upstream_broker != '127.0.0.1' else 1884
    downstream_port = 1883 if downstream_broker != '127.0.0.1' else 1885
    
    # 检查MQTT broker连接
    print("检查MQTT broker连接...")
    if not check_broker_connection(upstream_broker, upstream_port):
        print(f"❌ 无法连接到upstream broker ({upstream_broker}:{upstream_port})")
        return
    if not check_broker_connection(downstream_broker, downstream_port):
        print(f"❌ 无法连接到downstream broker ({downstream_broker}:{downstream_port})")
        return
    print("✅ MQTT brokers连接正常")
    
    test = PerformanceTest()
    results = {}
    
    # 多场景吞吐量测试
    test_scenarios = [
        (200, 50, "轻负载"),
        (2000, 100, "中等负载"),
        (10000, 200, "重负载")
    ]
    
    results['throughput_tests'] = []
    
    for message_count, message_size, scenario_name in test_scenarios:
        print("=" * 50)
        print(f"吞吐量测试 - {scenario_name}")
        throughput_result = test.run_throughput_test(message_count, message_size)
        throughput_result['scenario'] = scenario_name
        results['throughput_tests'].append(throughput_result)
        
        print(f"  场景: {scenario_name}")
        print(f"  发送: {throughput_result['sent']} 条")
        print(f"  接收: {throughput_result['received']} 条")
        print(f"  耗时: {throughput_result['duration']:.2f} 秒")
        print(f"  吞吐量: {throughput_result['throughput']:.2f} 消息/秒")
        print(f"  丢失率: {throughput_result['loss_rate']:.2f}%")
    
    # 延迟测试
    print("=" * 50)
    latency_result = test.run_latency_test(100)
    if latency_result:
        results['latency'] = latency_result
        print(f"延迟测试结果:")
        print(f"  样本数: {latency_result['samples']}")
        print(f"  平均延迟: {latency_result['avg_latency_ms']:.2f} ms")
        print(f"  最小延迟: {latency_result['min_latency_ms']:.2f} ms")
        print(f"  最大延迟: {latency_result['max_latency_ms']:.2f} ms")
        print(f"  P95延迟: {latency_result['p95_latency_ms']:.2f} ms")
        print(f"  P99延迟: {latency_result['p99_latency_ms']:.2f} ms")
    
    # 保存结果
    with open('results/performance_results.json', 'w') as f:
        json.dump(results, f, indent=2)
    
    # 生成详细报告
    with open('results/performance_report.md', 'w') as f:
        f.write("# MQTT Forwarder Performance Test Report\n\n")
        f.write(f"**测试时间**: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"**测试环境**: Docker容器\n")
        f.write(f"**Broker配置**: {upstream_broker}:{upstream_port} ↔ {downstream_broker}:{downstream_port}\n\n")
        
        f.write("## 吞吐量测试结果\n\n")
        f.write("| 场景 | 消息数 | 消息大小 | 发送数 | 接收数 | 耗时(s) | 吞吐量(msg/s) | 丢失率(%) |\n")
        f.write("|------|--------|----------|--------|--------|---------|---------------|----------|\n")
        
        for result in results['throughput_tests']:
            f.write(f"| {result['scenario']} | {result.get('message_count', 'N/A')} | {result.get('message_size', 'N/A')}B | ")
            f.write(f"{result['sent']} | {result['received']} | {result['duration']:.2f} | ")
            f.write(f"{result['throughput']:.2f} | {result['loss_rate']:.2f} |\n")
        
        if latency_result:
            f.write("\n## 延迟测试结果\n\n")
            f.write(f"- **样本数**: {latency_result['samples']}\n")
            f.write(f"- **平均延迟**: {latency_result['avg_latency_ms']:.2f} ms\n")
            f.write(f"- **最小延迟**: {latency_result['min_latency_ms']:.2f} ms\n")
            f.write(f"- **最大延迟**: {latency_result['max_latency_ms']:.2f} ms\n")
            f.write(f"- **P95延迟**: {latency_result['p95_latency_ms']:.2f} ms\n")
            f.write(f"- **P99延迟**: {latency_result['p99_latency_ms']:.2f} ms\n")
        
        f.write("\n## 性能评估\n\n")
        best_throughput = max(results['throughput_tests'], key=lambda x: x['throughput'])
        f.write(f"- **最佳吞吐量**: {best_throughput['throughput']:.2f} 消息/秒 ({best_throughput['scenario']})\n")
        if latency_result:
            f.write(f"- **延迟评级**: {'优秀' if latency_result['avg_latency_ms'] < 10 else '良好' if latency_result['avg_latency_ms'] < 50 else '一般'}\n")
    
    print("=" * 50)
    print("测试完成，结果已保存到 results/ 目录")

if __name__ == "__main__":
    main()
