#!/usr/bin/env python3
import paho.mqtt.client as mqtt
import json
import time
import threading
import statistics
import os

class PerformanceTest:
    def __init__(self):
        self.received_messages = []
        self.sent_count = 0
        self.received_count = 0
        self.start_time = None
        self.lock = threading.Lock()
        
    def on_connect(self, client, userdata, flags, reason_code, properties):
        print(f"Connected with result code {reason_code}")
        client.subscribe("/test/perf/#")
        
    def on_message(self, client, userdata, msg):
        with self.lock:
            self.received_count += 1
            if self.start_time:
                latency = time.time() - self.start_time
                self.received_messages.append({
                    'timestamp': time.time(),
                    'latency': latency,
                    'payload_size': len(msg.payload)
                })
    
    def run_throughput_test(self, message_count=1000, message_size=100):
        """测试吞吐量"""
        print(f"开始吞吐量测试: {message_count} 条消息, 每条 {message_size} 字节")
        
        # 重置计数器
        with self.lock:
            self.received_count = 0
            self.sent_count = 0
            self.received_messages = []
        
        # 接收线程
        def receiver_thread():
            receiver = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, "perf_receiver")
            receiver.on_connect = self.on_connect
            receiver.on_message = self.on_message
            receiver.connect("127.0.0.1", 1884, 60)
            receiver.loop_forever()
        
        # 发送线程
        def sender_thread():
            time.sleep(2)  # 等待接收端连接
            sender = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, "perf_sender")
            sender.connect("127.0.0.1", 1885, 60)
            
            # 生成测试数据
            test_data = {"data": "x" * message_size}
            payload = json.dumps(test_data)
            
            # 开始发送
            for i in range(message_count):
                topic = f"/test/perf/device_{i % 10}"
                sender.publish(topic, payload)
                with self.lock:
                    self.sent_count += 1
                time.sleep(0.001)  # 控制发送速率
            
            sender.disconnect()
        
        # 启动线程
        receiver_t = threading.Thread(target=receiver_thread, daemon=True)
        sender_t = threading.Thread(target=sender_thread)
        
        self.start_time = time.time()
        start_time = time.time()
        
        receiver_t.start()
        sender_t.start()
        
        # 等待发送完成
        sender_t.join()
        
        # 等待接收完成
        timeout = 30
        while self.received_count < message_count and timeout > 0:
            time.sleep(0.1)
            timeout -= 0.1
            
        end_time = time.time()
        duration = end_time - start_time
        
        return {
            'sent': self.sent_count,
            'received': self.received_count,
            'duration': duration,
            'throughput': self.received_count / duration if duration > 0 else 0,
            'loss_rate': (self.sent_count - self.received_count) / self.sent_count * 100 if self.sent_count > 0 else 0
        }
    
    def run_latency_test(self, sample_count=100):
        """测试延迟"""
        print(f"开始延迟测试: {sample_count} 个样本")
        
        # 重置状态
        with self.lock:
            self.received_count = 0
            self.received_messages = []
        
        latencies = []
        
        # 接收线程
        def receiver_thread():
            receiver = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, "latency_receiver")
            receiver.on_connect = self.on_connect
            receiver.on_message = self.on_message
            receiver.connect("127.0.0.1", 1884, 60)
            receiver.loop_forever()
        
        # 启动接收线程
        receiver_t = threading.Thread(target=receiver_thread, daemon=True)
        receiver_t.start()
        
        time.sleep(2)  # 等待连接建立
        
        # 发送端
        sender = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, "latency_sender")
        sender.connect("127.0.0.1", 1885, 60)
        
        for i in range(sample_count):
            initial_count = self.received_count
            
            # 记录发送时间
            send_time = time.time()
            sender.publish(f"/test/perf/latency_{i}", json.dumps({"test": i, "send_time": send_time}))
            
            # 等待消息接收
            timeout = 5
            while self.received_count <= initial_count and timeout > 0:
                time.sleep(0.001)
                timeout -= 0.001
                
            # 计算延迟
            if self.received_count > initial_count:
                receive_time = time.time()
                latency = (receive_time - send_time) * 1000  # 转换为毫秒
                latencies.append(latency)
                
            time.sleep(0.01)  # 避免过快发送
            
        sender.disconnect()
        
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
    
    test = PerformanceTest()
    results = {}
    
    # 吞吐量测试
    print("=" * 50)
    throughput_result = test.run_throughput_test(1000, 100)
    results['throughput'] = throughput_result
    print(f"吞吐量测试结果:")
    print(f"  发送: {throughput_result['sent']} 条")
    print(f"  接收: {throughput_result['received']} 条")
    print(f"  耗时: {throughput_result['duration']:.2f} 秒")
    print(f"  吞吐量: {throughput_result['throughput']:.2f} 消息/秒")
    print(f"  丢失率: {throughput_result['loss_rate']:.2f}%")
    
    # 重置计数器
    test.received_count = 0
    test.sent_count = 0
    test.received_messages = []
    
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
    
    # 生成报告
    with open('results/performance_report.md', 'w') as f:
        f.write("# MQTT Forwarder Performance Test Report\n\n")
        f.write("## Throughput Test\n")
        f.write(f"- Messages sent: {throughput_result['sent']}\n")
        f.write(f"- Messages received: {throughput_result['received']}\n")
        f.write(f"- Duration: {throughput_result['duration']:.2f} seconds\n")
        f.write(f"- Throughput: {throughput_result['throughput']:.2f} messages/second\n")
        f.write(f"- Loss rate: {throughput_result['loss_rate']:.2f}%\n\n")
        
        if latency_result:
            f.write("## Latency Test\n")
            f.write(f"- Samples: {latency_result['samples']}\n")
            f.write(f"- Average latency: {latency_result['avg_latency_ms']:.2f} ms\n")
            f.write(f"- Min latency: {latency_result['min_latency_ms']:.2f} ms\n")
            f.write(f"- Max latency: {latency_result['max_latency_ms']:.2f} ms\n")
            f.write(f"- P95 latency: {latency_result['p95_latency_ms']:.2f} ms\n")
            f.write(f"- P99 latency: {latency_result['p99_latency_ms']:.2f} ms\n")
    
    print("=" * 50)
    print("测试完成，结果已保存到 results/ 目录")

if __name__ == "__main__":
    main()
