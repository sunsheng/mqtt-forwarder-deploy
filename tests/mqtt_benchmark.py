#!/usr/bin/env python3

import json
import random
import subprocess
import threading
import time
import os
import uuid
from datetime import datetime
from concurrent.futures import ThreadPoolExecutor

class MQTTBenchmark:
    def __init__(self, max_messages=10000):
        self.sent_count = 0
        self.received_count = 0
        self.received_messages = []
        self.lock = threading.Lock()
        self.start_time = None
        self.end_time = None
        self.max_messages = max_messages
        self.message_file = "all_messages.txt"
        
        # 一次性生成最大数量的消息
        self.generate_all_messages()
        
    def generate_all_messages(self):
        """一次性生成最大数量的消息文件"""
        print(f"一次性生成 {self.max_messages} 条动态消息...")
        with open(self.message_file, 'w') as f:
            for _ in range(self.max_messages):
                message = self.generate_json_message()
                f.write(message + '\n')
        print(f"消息文件 {self.message_file} 生成完成")
    
    def send_messages_from_file(self, message_count, topic_base="/test/perf/batch"):
        """从预生成的文件中发送指定数量的消息"""
        def send_batch():
            try:
                # 读取指定数量的消息
                with open(self.message_file, 'r') as f:
                    messages = []
                    for i, line in enumerate(f):
                        if i >= message_count:
                            break
                        messages.append(line)
                
                messages_text = ''.join(messages)
                
                # 通过标准输入传递给 mosquitto_pub -l
                cmd = [
                    'docker', 'compose', '-f', 'docker-compose.test.yml', 'exec', '-T',
                    'mqtt-broker-downstream', 'mosquitto_pub', '-h', 'localhost', 
                    '-t', topic_base, '-l', '-q', '0'
                ]
                
                process = subprocess.run(cmd, input=messages_text, text=True, 
                                       capture_output=True)
                
                if process.returncode == 0:
                    with self.lock:
                        self.sent_count = message_count
                else:
                    print(f"发送错误: {process.stderr}")
                    
            except Exception as e:
                print(f"发送线程错误: {e}")
        
        sender_thread = threading.Thread(target=send_batch)
        sender_thread.start()
        return sender_thread
        
    def generate_json_message(self):
        """生成动态大小的 JSON 数组消息"""
        array_size = random.randint(10, 200)
        current_time = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        
        message_array = []
        for i in range(array_size):
            data_point = {
                "name": f"RTU.COM{random.randint(1,4)}.T9-{random.randint(1,99):02d}-{random.randint(10,50)}.AI{i+1:02d}",
                "value": str(random.randint(1000, 9999)),
                "time": current_time
            }
            message_array.append(data_point)
        
        return json.dumps(message_array, separators=(',', ':'))
    
    def generate_message_file(self, count, filename):
        """生成消息文件，一行一个 JSON 数组"""
        print(f"生成 {count} 条动态消息到 {filename}...")
        with open(filename, 'w') as f:
            for _ in range(count):
                message = self.generate_json_message()
                f.write(message + '\n')
        return filename
    
    def start_receiver(self, message_count, scenario_name):
        """启动接收线程"""
        def receive_messages():
            try:
                cmd = [
                    'docker', 'compose', '-f', 'docker-compose.test.yml', 'exec', '-T',
                    'mqtt-broker-upstream', 'mosquitto_sub', '-h', 'localhost', 
                    '-t', '/ge/web/#', '-C', str(message_count), '-q', '0'
                ]
                
                process = subprocess.Popen(cmd, stdout=subprocess.PIPE, 
                                         stderr=subprocess.PIPE, text=True)
                
                for line in process.stdout:
                    with self.lock:
                        self.received_count += 1
                        self.received_messages.append(line.strip())
                        
                process.wait()
                
            except Exception as e:
                print(f"接收线程错误: {e}")
        
        receiver_thread = threading.Thread(target=receive_messages)
        receiver_thread.daemon = True
        receiver_thread.start()
        return receiver_thread
    
    def run_benchmark(self, message_count, scenario_name):
        """运行基准测试"""
        print("=" * 50)
        print(f"测试场景: {scenario_name}")
        print(f"消息数量: {message_count}")
        print(f"JSON数组大小: 10-200 随机")
        print("=" * 50)
        
        # 重置计数器
        self.sent_count = 0
        self.received_count = 0
        self.received_messages = []
        
        # 启动接收线程
        print("启动接收线程...")
        receiver_thread = self.start_receiver(message_count, scenario_name)
        
        # 等待接收线程准备就绪
        time.sleep(2)
        
        # 开始计时并发送消息
        print(f"开始批量发送 {message_count} 条消息...")
        self.start_time = time.time()
        
        sender_thread = self.send_messages_from_file(message_count, f"/ge/web/{uuid.uuid4().hex[:8]}")
        
        # 等待发送完成
        sender_thread.join()
        
        # 等待接收完成 (根据消息数量调整超时时间)
        timeout = max(60, message_count // 100)  # 至少60秒，大负载时更长
        print(f"等待接收完成 (超时: {timeout}秒)...")
        receiver_thread.join(timeout=timeout)
        
        self.end_time = time.time()
        
        # 计算性能指标
        duration = self.end_time - self.start_time
        throughput = message_count / duration if duration > 0 else 0
        loss_rate = (message_count - self.received_count) * 100 / message_count
        
        print(f"测试完成!")
        print(f"发送: {self.sent_count} 条")
        print(f"接收: {self.received_count} 条")
        print(f"耗时: {duration:.2f} 秒")
        print(f"吞吐量: {throughput:.2f} 消息/秒")
        print(f"丢失率: {loss_rate:.2f}%")
        print()
        
        # 显示示例消息
        if self.received_messages:
            print("示例接收消息:")
            msg = self.received_messages[0]
            if len(msg) > 200:
                # 显示前100个字符 + 省略号 + 后100个字符
                example = msg[:100] + "..." + msg[-100:]
            else:
                example = msg
            print(example)
            print()
        
        # 清理文件
        # 不需要清理，使用共享的消息文件
        
        return {
            'scenario': scenario_name,
            'sent': self.sent_count,
            'received': self.received_count,
            'duration': duration,
            'throughput': throughput,
            'loss_rate': loss_rate
        }
    
    def cleanup(self):
        """清理生成的消息文件"""
        if os.path.exists(self.message_file):
            os.remove(self.message_file)

def main():
    print("启动测试环境...")
    
    # 启动服务
    subprocess.run(['docker', 'compose', '-f', 'docker-compose.test.yml', 'up', '-d', 'mqtt-forwarder'])
    time.sleep(5)
    
    benchmark = MQTTBenchmark(max_messages=1000)  # 一次性生成1000条消息
    results = []
    
    # 运行不同场景的测试
    test_scenarios = [
        (100, "极轻负载"),
        (200, "轻负载"),
        (500, "中等负载"),
        (1000, "重负载")
    ]
    
    try:
        for message_count, scenario_name in test_scenarios:
            result = benchmark.run_benchmark(message_count, scenario_name)
            results.append(result)
    finally:
        # 清理消息文件
        benchmark.cleanup()
    
    # 清理环境
    print("清理测试环境...")
    subprocess.run(['docker', 'compose', '-f', 'docker-compose.test.yml', 'down'])
    
    print("基准测试完成!")
    print("\n=== 测试总结 ===")
    for result in results:
        print(f"{result['scenario']}: {result['throughput']:.2f} msg/s, 丢失率: {result['loss_rate']:.2f}%")

if __name__ == "__main__":
    main()
