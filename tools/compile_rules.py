#!/usr/bin/env python
"""
compile_rules.py — 构建期 YAML 规则模板 → JSON 编译 (供 C++ JsonFacade 运行时消费)

输入:
  astock_engine/rules/catalogs/trading_term_catalog.yaml   (160 模板索引)
  astock_engine/rules/examples/<file_name>                 (各模板 YAML 定义)

输出:
  config/rules/compiled.json  (单文件, 约 1-3MB)
  - 模板元信息 (template_id, display_name, phase, summary, actions, tags)
  - 规则实体 (version, namespace, rules[].id/name/stage/priority/when/then)

用法: python tools/compile_rules.py [--verbose]
"""

import argparse
import datetime
import json
import os
import sys

import yaml

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CATALOG_PATH = os.path.join(PROJECT_ROOT, "astock_engine", "rules", "catalogs",
                            "trading_term_catalog.yaml")
EXAMPLES_DIR = os.path.join(PROJECT_ROOT, "astock_engine", "rules", "examples")
OUTPUT_PATH = os.path.join(PROJECT_ROOT, "config", "rules", "compiled.json")


def load_catalog():
    with open(CATALOG_PATH, encoding="utf-8") as f:
        catalog = yaml.safe_load(f)
    if not catalog or "templates" not in catalog:
        print(f"错误: catalog 无效或无 templates 字段: {CATALOG_PATH}")
        sys.exit(1)
    return catalog


def load_yaml(filename):
    path = os.path.join(EXAMPLES_DIR, filename)
    if not os.path.exists(path):
        raise FileNotFoundError(f"模板文件缺失: {path}")
    with open(path, encoding="utf-8") as f:
        return yaml.safe_load(f.read())


def compile_rules(verbose=False):
    catalog = load_catalog()
    templates_out = []
    errors = []

    for entry in catalog["templates"]:
        template_id = entry.get("template_id", "")
        file_name = entry.get("file_name", "")
        if not template_id or not file_name:
            errors.append(f"catalog 条目缺 template_id 或 file_name: {entry}")
            continue

        try:
            yaml_def = load_yaml(file_name)
        except Exception as e:
            errors.append(f"加载 YAML 失败 [{file_name}]: {e}")
            continue

        compiled = {
            "templateId": template_id,
            "displayName": entry.get("display_name", ""),
            "fileName": file_name,
            "phase": entry.get("phase", ""),
            "summary": entry.get("summary", ""),
            "actions": entry.get("actions", []),
            "tags": entry.get("tags", []),
            "namespace": yaml_def.get("namespace", ""),
            "rules": yaml_def.get("rules", []),
        }
        templates_out.append(compiled)

        if verbose:
            rule_count = len(yaml_def.get("rules", []))
            print(f"  {template_id} ← {file_name} 规则数={rule_count}")

    compiled_json = {
        "version": catalog.get("version", 1),
        "namespace": catalog.get("namespace", ""),
        "compiled_at": datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S"),
        "template_count": len(templates_out),
        "templates": templates_out,
    }

    os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
    with open(OUTPUT_PATH, "w", encoding="utf-8") as f:
        json.dump(compiled_json, f, ensure_ascii=False, indent=2)

    # 内置规则已编译进 EXE 二进制 (embed_rules.py → rules_builtin.cpp), 不再拷贝到 bin/
    print(f"编译完成: {len(templates_out)} 个模板 → {OUTPUT_PATH} "
          f"({os.path.getsize(OUTPUT_PATH) / 1024:.0f} KB)")
    if errors:
        print(f"  警告: {len(errors)} 个错误")
        for e in errors:
            print(f"    - {e}")
    # errors 为阻塞性时 exit(1), 目前 YAML 文件缺失不影响已编译部分
    return len(templates_out)


def main():
    parser = argparse.ArgumentParser(description="规则模板 YAML→JSON 编译")
    parser.add_argument("--verbose", action="store_true", help="详细输出")
    args = parser.parse_args()
    compile_rules(verbose=args.verbose)


if __name__ == "__main__":
    main()
