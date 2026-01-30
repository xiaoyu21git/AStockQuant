#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
性能监控和告警系统
跟踪C++ Event系统的性能指标，检测性能下降
"""

import json
import subprocess
import sys
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Optional
import re

class PerformanceMonitor:
    def __init__(self, project_root: str = None):
        if project_root is None:
            project_root = Path(__file__).parent
        
        self.project_root = Path(project_root)
        self.baseline_file = self.project_root / ".performance_baseline.json"
        self.history_file = self.project_root / ".performance_history.json"
        self.test_exe = self.project_root / "build/tests/test_eventsystem.exe"
        
        # 性能基准 (来自测试结果)
        self.baseline = {
            "HighVolumeEventPublishing": 769231,      # events/sec
            "ConcurrentPublishers": 258065,            # 8线程 events/sec
            "PythonParity": 1495327,                   # 8线程×4订阅 events/sec
        }
        
        self.degradation_threshold = 0.10  # 10% 性能下降阈值
        self.critical_threshold = 0.20      # 20% 性能下降触发告警
        
        self.history = self._load_history()

    def _load_history(self) -> Dict:
        """加载性能历史"""
        if self.history_file.exists():
            try:
                with open(self.history_file, 'r') as f:
                    return json.load(f)
            except:
                pass
        return {"measurements": []}

    def _save_history(self):
        """保存性能历史"""
        with open(self.history_file, 'w') as f:
            json.dump(self.history, f, indent=2)

    def run_performance_tests(self) -> Dict[str, int]:
        """运行性能测试并收集指标"""
        metrics = {}
        
        tests = [
            ("EventStressTest.HighVolumeEventPublishing", "HighVolumeEventPublishing"),
            ("EventStressTest.ConcurrentPublishers", "ConcurrentPublishers"),
            ("EventStressTest.PythonParity", "PythonParity"),
        ]
        
        print("\n" + "="*60)
        print("📊 运行性能测试")
        print("="*60)
        
        for test_name, metric_name in tests:
            print(f"\n🏃 {metric_name}...", end=" ")
            
            try:
                result = subprocess.run(
                    [str(self.test_exe), f"--gtest_filter={test_name}"],
                    capture_output=True,
                    text=True,
                    timeout=120
                )
                
                # 提取吞吐量
                output = result.stdout + result.stderr
                match = re.search(r'Throughput:\s+(\d+(?:,\d+)*)\s+events/sec', output)
                
                if match:
                    value = int(match.group(1).replace(',', ''))
                    metrics[metric_name] = value
                    print(f"✅ {value:,} events/sec")
                else:
                    print("⚠️  无法提取数据")
            except Exception as e:
                print(f"❌ {e}")
        
        return metrics

    def check_degradation(self, current: Dict[str, int]) -> Dict:
        """检查性能下降"""
        results = {
            "timestamp": datetime.now().isoformat(),
            "metrics": current,
            "alerts": [],
            "status": "OK"
        }
        
        for metric_name, baseline_value in self.baseline.items():
            if metric_name not in current:
                continue
            
            current_value = current[metric_name]
            degradation = 1 - (current_value / baseline_value)
            
            print(f"\n📈 {metric_name}:")
            print(f"   基准: {baseline_value:,} events/sec")
            print(f"   当前: {current_value:,} events/sec")
            
            if degradation > 0:
                print(f"   ⚠️  下降: {degradation*100:.1f}%")
                
                if degradation > self.critical_threshold:
                    alert = {
                        "level": "CRITICAL",
                        "metric": metric_name,
                        "degradation_percent": round(degradation * 100, 1),
                        "baseline": baseline_value,
                        "current": current_value
                    }
                    results["alerts"].append(alert)
                    results["status"] = "CRITICAL"
                    print(f"   🚨 CRITICAL: 性能下降超过{self.critical_threshold*100:.0f}%")
                
                elif degradation > self.degradation_threshold:
                    alert = {
                        "level": "WARNING",
                        "metric": metric_name,
                        "degradation_percent": round(degradation * 100, 1),
                        "baseline": baseline_value,
                        "current": current_value
                    }
                    results["alerts"].append(alert)
                    if results["status"] != "CRITICAL":
                        results["status"] = "WARNING"
                    print(f"   ⚠️  WARNING: 性能下降超过{self.degradation_threshold*100:.0f}%")
            else:
                print(f"   ✅ 性能改进: {-degradation*100:.1f}%")
        
        return results

    def generate_report(self, results: Dict) -> str:
        """生成性能报告"""
        timestamp = results["timestamp"]
        
        report = f"""
{'='*70}
性能监控报告
{'='*70}
时间: {timestamp}
状态: {results['status']}

"""
        
        if results["alerts"]:
            report += f"🚨 告警 ({len(results['alerts'])} 个):\n"
            for alert in results["alerts"]:
                report += f"\n  【{alert['level']}】{alert['metric']}\n"
                report += f"    基准: {alert['baseline']:,} evt/s\n"
                report += f"    当前: {alert['current']:,} evt/s\n"
                report += f"    下降: {alert['degradation_percent']:.1f}%\n"
        
        report += f"""
性能指标:
"""
        for metric, value in results["metrics"].items():
            baseline = self.baseline.get(metric, 0)
            ratio = value / baseline if baseline > 0 else 0
            status = "✅" if ratio >= 0.9 else "⚠️" if ratio >= 0.8 else "❌"
            report += f"  {status} {metric}: {value:,} evt/s ({ratio*100:.0f}% of baseline)\n"
        
        report += f"{'='*70}\n"
        
        return report

    def run_monitoring(self, build_type: str = "Release") -> bool:
        """运行完整的监控流程"""
        print(f"\n🎯 性能监控 ({build_type} 版本)")
        
        # 1. 运行性能测试
        metrics = self.run_performance_tests()
        
        if not metrics:
            print("\n❌ 无法获取性能指标")
            return False
        
        # 2. 检查性能下降
        results = self.check_degradation(metrics)
        
        # 3. 记录历史
        self.history["measurements"].append(results)
        self._save_history()
        
        # 4. 生成报告
        report = self.generate_report(results)
        print(report)
        
        # 5. 保存报告
        report_file = self.project_root / f"performance_report_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt"
        with open(report_file, 'w', encoding='utf-8') as f:
            f.write(report)
        
        print(f"📝 报告已保存: {report_file}")
        
        # 6. 返回状态
        return results["status"] != "CRITICAL"

    def get_trend(self, metric_name: str, last_n: int = 10) -> Dict:
        """获取性能趋势"""
        measurements = self.history.get("measurements", [])
        
        trend = {
            "metric": metric_name,
            "baseline": self.baseline.get(metric_name, 0),
            "measurements": []
        }
        
        for m in measurements[-last_n:]:
            if metric_name in m.get("metrics", {}):
                trend["measurements"].append({
                    "timestamp": m["timestamp"],
                    "value": m["metrics"][metric_name]
                })
        
        return trend

    def print_history(self, metric_name: str = None, last_n: int = 10):
        """打印历史记录"""
        print(f"\n{'='*60}")
        print(f"📈 性能历史 (最近 {last_n} 次)")
        print(f"{'='*60}")
        
        measurements = self.history.get("measurements", [])
        
        for m in measurements[-last_n:]:
            timestamp = m["timestamp"]
            status = m["status"]
            
            print(f"\n⏰ {timestamp} [{status}]")
            
            for name, value in m.get("metrics", {}).items():
                if metric_name and metric_name != name:
                    continue
                
                baseline = self.baseline.get(name, 0)
                ratio = value / baseline if baseline > 0 else 0
                status_icon = "✅" if ratio >= 0.9 else "⚠️" if ratio >= 0.8 else "❌"
                
                print(f"  {status_icon} {name}: {value:,} evt/s ({ratio*100:.0f}%)")


def main():
    import argparse
    
    parser = argparse.ArgumentParser(description="性能监控和告警系统")
    parser.add_argument("-m", "--monitor", action="store_true",
                       help="运行性能监控")
    parser.add_argument("-b", "--build-type", default="Release",
                       choices=["Debug", "Release"],
                       help="CMake 构建类型")
    parser.add_argument("-i", "--history", action="store_true",
                       help="显示性能历史")
    parser.add_argument("-t", "--test", type=str,
                       help="显示特定测试的历史")
    
    args = parser.parse_args()
    
    monitor = PerformanceMonitor()
    
    if args.monitor:
        return monitor.run_monitoring(args.build_type)
    elif args.history:
        monitor.print_history(args.test)
        return True
    else:
        parser.print_help()
        return False


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
