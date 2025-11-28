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

class ResourceStressTest:
    def __init__(self):
        self.container_name = "tests-mqtt-forwarder-1"
        self.message_file = "stress_messages.txt"
        
    def generate_messages(self, count):
        """生成指定数量的测试消息"""
        print(f"生成 {count} 条测试消息...")
        with open(self.message_file, 'w') as f:
            for _ in range(count):
                message = self.generate_json_message()
                f.write(message + '\n')
        print(f"消息文件生成完成")
    
    def generate_json_message(self):
        """生成动态大小的 JSON 数组消息"""
        array_size = random.randint(50, 300)  # 更大的消息增加处理压力
        current_time = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        
        message_array = []
        for i in range(array_size):
            data_point = {
                "deviceId": f"device_{random.randint(1000, 9999)}",
                "sensorName": f"RTU.COM{random.randint(1,8)}.T9-{random.randint(1,99):02d}-{random.randint(10,99)}.AI{i+1:02d}",
                "value": random.uniform(0.1, 999.9),
                "unit": random.choice(["°C", "Pa", "V", "A", "Hz", "%"]),
                "quality": random.choice(["good", "uncertain", "bad"]),
                "timestamp": current_time,
                "metadata": {
                    "location": f"zone_{random.randint(1,10)}",
                    "type": random.choice(["temperature", "pressure", "voltage", "current"]),
                    "calibrated": random.choice([True, False])
                }
            }
            message_array.append(data_point)
        
        return json.dumps(message_array, separators=(',', ':'))
    
    def get_container_stats(self):
        """获取容器的CPU和内存使用情况"""
        try:
            # 获取容器统计信息
            cmd = ['docker', 'stats', '--no-stream', '--format', 
                   '{{.Container}},{{.CPUPerc}},{{.MemUsage}},{{.MemPerc}}', 
                   self.container_name]
            
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode == 0 and result.stdout.strip():
                line = result.stdout.strip()
                parts = line.split(',')
                if len(parts) >= 4:
                    cpu_percent = parts[1].replace('%', '').strip()
                    mem_usage = parts[2].strip()  # 格式: "used / limit"
                    mem_percent = parts[3].replace('%', '').strip()
                    
                    return {
                        'cpu_percent': float(cpu_percent) if cpu_percent != '--' and cpu_percent else 0.0,
                        'memory_usage': mem_usage,
                        'memory_percent': float(mem_percent) if mem_percent != '--' and mem_percent else 0.0
                    }
        except Exception as e:
            print(f"获取容器统计信息失败: {e}")
        
        return {'cpu_percent': 0.0, 'memory_usage': 'N/A', 'memory_percent': 0.0}
    
    def monitor_resources(self, duration, interval=0.5):
        """监控资源使用情况"""
        stats_history = []
        start_time = time.time()
        
        while time.time() - start_time < duration:
            stats = self.get_container_stats()
            stats['timestamp'] = time.time() - start_time
            stats_history.append(stats)
            print(f"监控: CPU {stats['cpu_percent']:.1f}%, 内存 {stats['memory_percent']:.1f}%")
            time.sleep(interval)
        
        return stats_history
    
    def send_message_batch(self, batch_size, topic_base="/ge/web"):
        """发送一批消息"""
        try:
            # 读取指定数量的消息
            with open(self.message_file, 'r') as f:
                messages = []
                for i, line in enumerate(f):
                    if i >= batch_size:
                        break
                    messages.append(line)
            
            messages_text = ''.join(messages)
            topic = f"{topic_base}/{uuid.uuid4().hex[:8]}"
            
            # 通过标准输入传递给 mosquitto_pub -l
            cmd = [
                'docker', 'compose', '-f', 'docker-compose.test.yml', 'exec', '-T',
                'mqtt-broker-downstream', 'mosquitto_pub', '-h', 'localhost', 
                '-t', topic, '-l', '-q', '0'
            ]
            
            process = subprocess.run(cmd, input=messages_text, text=True, 
                                   capture_output=True)
            
            return process.returncode == 0
            
        except Exception as e:
            print(f"发送消息批次错误: {e}")
            return False
    
    def run_stress_test(self, batch_size, batch_count, scenario_name):
        """运行压力测试"""
        print("=" * 60)
        print(f"压力测试场景: {scenario_name}")
        print(f"每批消息数: {batch_size}")
        print(f"批次数量: {batch_count}")
        print(f"总消息数: {batch_size * batch_count}")
        print("=" * 60)
        
        # 生成足够的消息
        self.generate_messages(batch_size)
        
        # 获取基线资源使用情况
        print("获取基线资源使用情况...")
        baseline_stats = self.get_container_stats()
        print(f"基线 - CPU: {baseline_stats['cpu_percent']:.2f}%, 内存: {baseline_stats['memory_usage']}")
        
        # 开始监控
        monitor_duration = batch_count * 2 + 10  # 预估监控时间
        print(f"开始资源监控 (预计 {monitor_duration} 秒)...")
        
        # 启动资源监控线程
        monitor_thread = threading.Thread(
            target=lambda: setattr(self, 'stats_history', 
                                  self.monitor_resources(monitor_duration))
        )
        monitor_thread.start()
        
        # 等待监控线程启动
        time.sleep(2)
        
        # 开始发送消息批次
        print(f"开始发送 {batch_count} 个批次...")
        start_time = time.time()
        successful_batches = 0
        
        for i in range(batch_count):
            print(f"发送批次 {i+1}/{batch_count} ({batch_size} 条消息)...")
            
            if self.send_message_batch(batch_size):
                successful_batches += 1
        
        end_time = time.time()
        duration = end_time - start_time
        
        # 等待监控完成
        monitor_thread.join(timeout=5)
        
        # 分析资源使用情况
        stats_history = getattr(self, 'stats_history', [])
        
        if stats_history:
            max_cpu = max(s['cpu_percent'] for s in stats_history)
            avg_cpu = sum(s['cpu_percent'] for s in stats_history) / len(stats_history)
            max_mem = max(s['memory_percent'] for s in stats_history)
            avg_mem = sum(s['memory_percent'] for s in stats_history) / len(stats_history)
        else:
            max_cpu = avg_cpu = max_mem = avg_mem = 0.0
        
        # 输出结果
        print(f"\n测试完成!")
        print(f"成功批次: {successful_batches}/{batch_count}")
        print(f"总耗时: {duration:.2f} 秒")
        print(f"平均批次耗时: {duration/batch_count:.2f} 秒")
        print(f"\n资源使用情况:")
        print(f"CPU使用率 - 最大: {max_cpu:.2f}%, 平均: {avg_cpu:.2f}%")
        print(f"内存使用率 - 最大: {max_mem:.2f}%, 平均: {avg_mem:.2f}%")
        
        # 获取最终资源状态
        final_stats = self.get_container_stats()
        print(f"最终状态 - CPU: {final_stats['cpu_percent']:.2f}%, 内存: {final_stats['memory_usage']}")
        print()
        
        return {
            'scenario': scenario_name,
            'batch_size': batch_size,
            'batch_count': batch_count,
            'total_messages': batch_size * batch_count,
            'successful_batches': successful_batches,
            'duration': duration,
            'max_cpu': max_cpu,
            'avg_cpu': avg_cpu,
            'max_memory': max_mem,
            'avg_memory': avg_mem,
            'baseline_cpu': baseline_stats['cpu_percent'],
            'baseline_memory': baseline_stats['memory_percent']
        }
    
    def cleanup(self):
        """清理生成的文件"""
        if os.path.exists(self.message_file):
            os.remove(self.message_file)

def main():
    print("启动MQTT转发器压力测试...")
    
    # 启动测试环境
    print("启动测试环境...")
    subprocess.run(['docker', 'compose', '-f', 'docker-compose.test.yml', 'up', '-d'])
    time.sleep(10)  # 等待服务完全启动
    
    stress_test = ResourceStressTest()
    results = []
    
    # 测试场景: 500和1000两个批次大小，分别测试10、50、100批次
    test_scenarios = [
        (500, 10, "轻度压力测试 - 500x10 (5K消息)"),
        (500, 50, "中度压力测试 - 500x50 (25K消息)"), 
        (500, 100, "重度压力测试 - 500x100 (50K消息)"),
        (1000, 10, "轻度压力测试 - 1000x10 (10K消息)"),
        (1000, 50, "中度压力测试 - 1000x50 (50K消息)"),
        (1000, 100, "重度压力测试 - 1000x100 (100K消息)")
    ]
    
    try:
        for batch_size, batch_count, scenario_name in test_scenarios:
            result = stress_test.run_stress_test(batch_size, batch_count, scenario_name)
            results.append(result)
            
            # 测试间隔，让系统恢复
            print("等待系统恢复...")
            time.sleep(10)
            
    finally:
        # 清理
        stress_test.cleanup()
    
    # 清理测试环境
    print("清理测试环境...")
    subprocess.run(['docker', 'compose', '-f', 'docker-compose.test.yml', 'down'])
    
    # 输出测试总结
    print("\n" + "=" * 80)
    print("压力测试总结")
    print("=" * 80)
    
    for result in results:
        print(f"\n{result['scenario']}:")
        print(f"  消息处理: {result['successful_batches']}/{result['batch_count']} 批次成功")
        print(f"  总消息数: {result['total_messages']:,} 条")
        print(f"  处理时间: {result['duration']:.2f} 秒")
        print(f"  CPU峰值: {result['max_cpu']:.2f}% (平均: {result['avg_cpu']:.2f}%)")
        print(f"  内存峰值: {result['max_memory']:.2f}% (平均: {result['avg_memory']:.2f}%)")
        print(f"  吞吐量: {result['total_messages']/result['duration']:.0f} 消息/秒")

if __name__ == "__main__":
    main()
