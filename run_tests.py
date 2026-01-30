#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
C++ Event 系统 - 本地测试运行脚本
支持快速的编译、运行和验证
"""

import os
import sys
import subprocess
import json
import time
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Tuple

class TestRunner:
    def __init__(self, project_root: str = None):
        if project_root is None:
            project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        
        self.project_root = Path(project_root)
        self.build_dir = self.project_root / "build"
        self.tests_dir = self.project_root / "tests"
        self.bin_dir = self.build_dir / "tests"
        self.test_exe = self.bin_dir / "test_eventsystem.exe"
        
        self.results = {
            "timestamp": datetime.now().isoformat(),
            "configuration": {},
            "tests": {},
            "performance": {},
            "summary": {}
        }

    def configure(self, build_type: str = "Debug") -> bool:
        """使用CMake配置项目"""
        print(f"\n{'='*60}")
        print(f"🔧 配置 CMake (构建类型: {build_type})")
        print(f"{'='*60}")
        
        self.build_dir.mkdir(exist_ok=True)
        
        cmd = [
            "cmake",
            "..",
            "-G", "Visual Studio 16 2019",
            f"-DCMAKE_BUILD_TYPE={build_type}",
            "-DENABLE_TESTING=ON",
            "-DCMAKE_CXX_STANDARD=11"
        ]
        
        try:
            result = subprocess.run(
                cmd,
                cwd=str(self.build_dir),
                capture_output=True,
                text=True,
                timeout=60
            )
            
            if result.returncode == 0:
                print("✅ CMake 配置成功")
                self.results["configuration"]["cmake"] = "success"
                return True
            else:
                print(f"❌ CMake 配置失败")
                print(result.stderr)
                self.results["configuration"]["cmake"] = "failed"
                return False
        except Exception as e:
            print(f"❌ 错误: {e}")
            self.results["configuration"]["cmake"] = f"error: {str(e)}"
            return False

    def build(self, build_type: str = "Debug") -> bool:
        """编译测试代码"""
        print(f"\n{'='*60}")
        print(f"🔨 编译测试 (构建类型: {build_type})")
        print(f"{'='*60}")
        
        cmd = [
            "cmake",
            "--build", ".",
            "--target", "test_eventsystem",
            "--config", build_type,
            "-j4"
        ]
        
        try:
            result = subprocess.run(
                cmd,
                cwd=str(self.build_dir),
                capture_output=True,
                text=True,
                timeout=300
            )
            
            if result.returncode == 0 and self.test_exe.exists():
                print("✅ 编译成功")
                print(f"   可执行文件: {self.test_exe}")
                self.results["configuration"]["build"] = "success"
                return True
            else:
                print(f"❌ 编译失败")
                if result.stderr:
                    print(result.stderr[-500:])  # 最后500字符
                self.results["configuration"]["build"] = "failed"
                return False
        except Exception as e:
            print(f"❌ 错误: {e}")
            self.results["configuration"]["build"] = f"error: {str(e)}"
            return False

    def run_tests(self, filter_pattern: str = "*") -> bool:
        """运行测试"""
        print(f"\n{'='*60}")
        print(f"🧪 运行测试 (过滤: {filter_pattern})")
        print(f"{'='*60}")
        
        if not self.test_exe.exists():
            print(f"❌ 测试可执行文件不存在: {self.test_exe}")
            return False
        
        cmd = [
            str(self.test_exe),
            f"--gtest_filter={filter_pattern}",
            "--gtest_color=yes"
        ]
        
        try:
            start_time = time.time()
            
            result = subprocess.run(
                cmd,
                cwd=str(self.bin_dir),
                capture_output=True,
                text=True,
                timeout=600
            )
            
            elapsed = time.time() - start_time
            
            # 解析输出
            output = result.stdout + result.stderr
            self._parse_test_output(output)
            
            # 生成XML报告
            self._generate_xml_report()
            
            if result.returncode == 0:
                print("\n✅ 所有测试通过")
                return True
            else:
                print(f"\n⚠️  部分测试失败或禁用")
                print(f"   耗时: {elapsed:.2f}秒")
                # 仍然返回True因为禁用的测试不算失败
                return result.returncode in [0]
        except subprocess.TimeoutExpired:
            print("❌ 测试超时")
            self.results["summary"]["status"] = "timeout"
            return False
        except Exception as e:
            print(f"❌ 错误: {e}")
            self.results["summary"]["status"] = f"error: {str(e)}"
            return False

    def _parse_test_output(self, output: str):
        """解析测试输出"""
        lines = output.split('\n')
        
        for line in lines:
            # 捕获测试摘要
            if "tests from" in line and "ran" in line:
                print(f"\n📊 {line.strip()}")
            
            # 捕获性能数据
            elif "Throughput:" in line or "events/sec" in line:
                print(f"   📈 {line.strip()}")
                self._extract_performance(line)
            
            # 捕获通过/失败
            elif "PASSED" in line or "FAILED" in line:
                print(f"   {line.strip()}")

    def _extract_performance(self, line: str):
        """提取性能指标"""
        import re
        
        # 提取吞吐量 (events/sec)
        match = re.search(r'(\d+(?:,\d+)*)\s+events/sec', line)
        if match:
            value = int(match.group(1).replace(',', ''))
            if "Throughput" in line:
                self.results["performance"]["throughput"] = value

    def _generate_xml_report(self):
        """生成XML测试报告"""
        try:
            result = subprocess.run(
                [str(self.test_exe), "--gtest_output=xml:test_results.xml"],
                cwd=str(self.bin_dir),
                capture_output=True,
                timeout=60
            )
            
            xml_file = self.bin_dir / "test_results.xml"
            if xml_file.exists():
                print(f"\n📝 测试报告: {xml_file}")
        except:
            pass

    def validate_performance(self, baseline: Dict[str, int] = None) -> bool:
        """验证性能指标"""
        if baseline is None:
            baseline = {
                "throughput": 700000  # 700K events/sec (Release版本目标)
            }
        
        print(f"\n{'='*60}")
        print(f"📊 性能验证")
        print(f"{'='*60}")
        
        if "throughput" not in self.results["performance"]:
            print("⚠️  未能获取吞吐量数据")
            return True  # 不算失败
        
        current = self.results["performance"]["throughput"]
        target = baseline.get("throughput", 700000)
        
        print(f"当前吞吐量: {current:,} events/sec")
        print(f"目标吞吐量: {target:,} events/sec")
        
        degradation = ((target - current) / target * 100) if current < target else 0
        
        if degradation > 0:
            print(f"⚠️  性能下降: {degradation:.1f}%")
            
            if degradation > 10:
                print("❌ 性能下降超过10% - 构建失败")
                return False
        else:
            print(f"✅ 性能达到或超过目标")
        
        return True

    def generate_report(self, output_file: str = None) -> str:
        """生成总结报告"""
        if output_file is None:
            output_file = str(self.project_root / "test_report.json")
        
        with open(output_file, 'w', encoding='utf-8') as f:
            json.dump(self.results, f, indent=2, ensure_ascii=False)
        
        print(f"\n📋 报告已保存: {output_file}")
        return output_file

    def run_full_pipeline(self, build_type: str = "Debug") -> bool:
        """运行完整流程"""
        print("\n" + "="*60)
        print(f"🚀 开始 C++ Event 系统测试流程 ({build_type})")
        print("="*60)
        
        steps = [
            ("配置", lambda: self.configure(build_type)),
            ("编译", lambda: self.build(build_type)),
            ("测试", lambda: self.run_tests()),
            ("性能验证", self.validate_performance),
        ]
        
        for step_name, step_func in steps:
            if not step_func():
                print(f"\n❌ {step_name}失败，终止流程")
                return False
        
        self.results["summary"]["status"] = "success"
        self.generate_report()
        
        print("\n" + "="*60)
        print("✅ 所有步骤完成")
        print("="*60)
        return True


def main():
    import argparse
    
    parser = argparse.ArgumentParser(description="C++ Event 系统测试运行器")
    parser.add_argument("-b", "--build-type", default="Debug", 
                       choices=["Debug", "Release"],
                       help="CMake 构建类型 (默认: Debug)")
    parser.add_argument("-t", "--tests", default="*",
                       help="运行特定测试 (默认: 所有)")
    parser.add_argument("-c", "--configure", action="store_true",
                       help="仅配置 CMake")
    parser.add_argument("-d", "--build", action="store_true",
                       help="仅编译")
    parser.add_argument("-r", "--run", action="store_true",
                       help="仅运行测试")
    parser.add_argument("-p", "--performance", action="store_true",
                       help="仅验证性能")
    parser.add_argument("-a", "--all", action="store_true",
                       help="运行完整流程 (默认)")
    
    args = parser.parse_args()
    
    runner = TestRunner()
    
    # 确定运行模式
    if not any([args.configure, args.build, args.run, args.performance]):
        args.all = True
    
    if args.all:
        return runner.run_full_pipeline(args.build_type)
    
    success = True
    if args.configure:
        success &= runner.configure(args.build_type)
    if args.build:
        success &= runner.build(args.build_type)
    if args.run:
        success &= runner.run_tests(args.tests)
    if args.performance:
        success &= runner.validate_performance()
    
    return success


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
