#!/usr/bin/env python
"""
embed_rules.py — 构建期: 将 compiled.json 嵌入 C++ 源文件

输入:
  config/rules/compiled.json  (compile_rules.py 的产出, 唯一数据源)

输出:
  rules_builtin.cpp (C++ 源文件, 嵌入字节数组)

用法: python tools/embed_rules.py [--verbose] [--input compiled.json] [--output rules_builtin.cpp]
"""

import argparse
import json
import os
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_INPUT = os.path.join(PROJECT_ROOT, "config", "rules", "compiled.json")


def compile_rules_json(input_path, verbose=False):
    """直接读取 compiled.json, 不做任何转换 (YAML→JSON 由 compile_rules.py 负责)"""
    if not os.path.exists(input_path):
        print(f"错误: {input_path} 不存在, 请先运行 compile_rules.py", file=sys.stderr)
        sys.exit(1)

    with open(input_path, encoding="utf-8") as f:
        compiled = json.load(f)

    if not compiled or "templates" not in compiled:
        print("错误: compiled.json 无效", file=sys.stderr)
        sys.exit(1)

    if verbose:
        print(f"读取: {input_path} ({compiled['template_count']} 模板)")

    return json.dumps(compiled, ensure_ascii=False, indent=2)


def generate_cpp(rules_json_str, output_path, verbose=False):
    """将 JSON 字符串嵌入为 unsigned char 数组 (无长度限制, 编译后为 .rodata)"""
    json_bytes = rules_json_str.encode('utf-8')
    total_bytes = len(json_bytes)

    # 每行最多 16 个字节, 每个字节格式: 0xHH,
    cpp_lines = [
        "// 由 tools/embed_rules.py 自动生成, 勿手动编辑",
        "// 包含所有内置规则的编译后 JSON (字节数组, 编译进 .rodata)",
        "",
        "#include <cstddef>",
        "#include <string>",
        "",
        "namespace domain::strategy::rules::builtin {",
        "",
        "namespace {",
        f"const unsigned char kRulesData[{total_bytes + 1}] = {{",
    ]

    for i in range(0, total_bytes, 16):
        row_bytes = json_bytes[i:i+16]
        hex_row = ", ".join(f"0x{b:02X}" for b in row_bytes)
        if i + 16 < total_bytes:
            cpp_lines.append(f"    {hex_row},")
        else:
            cpp_lines.append(f"    {hex_row},")
            cpp_lines.append(f"    0x00  // null terminator")

    cpp_lines.append("};")
    cpp_lines.append("")
    cpp_lines.append("} // namespace")
    cpp_lines.append("")

    # 访问函数
    cpp_lines.append("const char* getBuiltinRulesJson() {")
    cpp_lines.append("    return reinterpret_cast<const char*>(kRulesData);")
    cpp_lines.append("}")

    cpp_lines.append("")
    cpp_lines.append("} // namespace domain::strategy::rules::builtin")

    cpp_content = "\n".join(cpp_lines) + "\n"

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(cpp_content)

    if verbose:
        size_kb = len(cpp_content) / 1024
        print(f"生成: {output_path} ({size_kb:.0f} KB, {total_bytes} 字节)")


def main():
    parser = argparse.ArgumentParser(description="嵌入 compiled.json 到 C++ 源文件")
    parser.add_argument("--input", default=DEFAULT_INPUT, help=f"compiled.json 路径 (默认: {DEFAULT_INPUT})")
    parser.add_argument("--output", required=True, help="输出 .cpp 文件路径")
    parser.add_argument("--verbose", action="store_true", help="详细输出")
    args = parser.parse_args()

    rules_json = compile_rules_json(args.input, verbose=args.verbose)
    generate_cpp(rules_json, args.output, verbose=args.verbose)
    print(f"嵌入完成 → {args.output}")


if __name__ == "__main__":
    main()
