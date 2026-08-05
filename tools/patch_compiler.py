#!/usr/bin/env python
"""
patch_compiler.py — 规则补丁编译器: YAML 变更描述 → 加密 .rulepatch 文件

输入:
  patch_rules.yaml  (变更描述 YAML)
  环境变量 RULE_KEY_SEED  (32-byte hex 密钥种子)

输出:
  .rulepatch 文件 (AES-256-GCM 加密 + HMAC-SHA256 签名)

用法:
  python tools/patch_compiler.py --input fix_v2.yaml --output fix_v2.rulepatch --version 2

安全约束:
  - 密钥种子通过环境变量注入, 不存储于代码仓库
  - 在受控构建服务器上运行
"""

import argparse
import hashlib
import hmac
import json
import os
import struct
import sys
import yaml

from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 补丁文件常量
MAGIC = 0x50525141  # "AQRP"
HEADER_SIZE = 64
HMAC_SIZE = 32
SALT_UUID = "a7f3c9e1-2b4d-5e6f-7890-abcd12345678"  # 编译时固定, 与 C++ 一致


def derive_master_key(seed_hex: str) -> bytes:
    """HKDF-SHA256 派生 32-byte AES 密钥"""
    seed = bytes.fromhex(seed_hex)
    if len(seed) != 32:
        raise ValueError(f"种子必须是 32 bytes (64 hex chars), 实际: {len(seed)} bytes")
    hkdf = HKDF(
        algorithm=hashes.SHA256(),
        length=32,
        salt=SALT_UUID.encode('ascii'),
        info=b"astock_rule_patch_v1",
    )
    return hkdf.derive(seed)


def load_patch_yaml(path: str) -> dict:
    """加载变更描述 YAML"""
    with open(path, encoding='utf-8') as f:
        return yaml.safe_load(f)


def build_patch_payload(operations: list) -> bytes:
    """构建待加密的 JSON payload"""
    payload = {"operations": operations}
    return json.dumps(payload, ensure_ascii=False, indent=2).encode('utf-8')


def compile_patch(input_path: str, output_path: str, patch_version: int, verbose: bool):
    """主编译流程"""
    # 1. 获取密钥
    seed = os.environ.get("RULE_KEY_SEED")
    if not seed:
        print("错误: 环境变量 RULE_KEY_SEED 未设置", file=sys.stderr)
        print("用法: RULE_KEY_SEED=<64-hex-chars> python tools/patch_compiler.py ...", file=sys.stderr)
        sys.exit(1)
    master_key = derive_master_key(seed)

    # 2. 加载变更描述
    patch_data = load_patch_yaml(input_path)
    operations = patch_data.get("operations", [])
    if not operations:
        print("错误: patch YAML 无 operations 字段", file=sys.stderr)
        sys.exit(1)

    # 校验操作
    valid_ops = {"add", "modify", "disable", "enable"}
    for op in operations:
        op_type = op.get("op", "")
        if op_type not in valid_ops:
            print(f"错误: 无效操作类型 '{op_type}', 允许: {valid_ops}", file=sys.stderr)
            sys.exit(1)
        if op_type == "modify":
            # 深度合并校验: fields 中的 key 必须在原始规则中存在
            # (此处仅做格式检查, 运行时由 C++ 做完整校验)
            if "ruleId" not in op or "fields" not in op:
                print(f"错误: modify 操作需要 ruleId 和 fields", file=sys.stderr)
                sys.exit(1)

    if verbose:
        print(f"操作数: {len(operations)}")
        for op in operations:
            print(f"  {op.get('op')}: {op.get('ruleId', op.get('template', {}).get('templateId', '?'))}")

    # 3. 构建 payload
    payload = build_patch_payload(operations)
    if verbose:
        print(f"Payload: {len(payload)} bytes")

    # 4. AES-256-GCM 加密
    aesgcm = AESGCM(master_key)
    iv = os.urandom(12)
    ciphertext_with_tag = aesgcm.encrypt(iv, payload, None)  # [密文][16-byte auth tag]

    # 5. 构建 patch 体 (header + ciphertext+tag), 但不含 HMAC
    payload_total_len = len(ciphertext_with_tag)
    header = struct.pack(
        "<I I I I 12s 36s",  # 4+4+4+4+12+36 = 64 bytes
        MAGIC,           # magic
        1,               # version
        patch_version,   # patch_version
        payload_total_len,  # payload_len (ciphertext + auth tag)
        iv,              # 12 bytes nonce
        b'\x00' * 40,    # reserved
    )

    if len(header) != HEADER_SIZE:
        print(f"内部错误: header 大小 {len(header)} != {HEADER_SIZE}", file=sys.stderr)
        sys.exit(1)

    patch_body = header + ciphertext_with_tag

    # 6. HMAC-SHA256 签名 (覆盖 header + ciphertext+tag)
    hmac_sig = hmac.digest(master_key, patch_body, hashlib.sha256)
    if len(hmac_sig) != HMAC_SIZE:
        print(f"内部错误: HMAC 大小 {len(hmac_sig)} != {HMAC_SIZE}", file=sys.stderr)
        sys.exit(1)

    # 7. 写入 .rulepatch 文件
    total_size = len(patch_body) + HMAC_SIZE
    with open(output_path, 'wb') as f:
        f.write(patch_body)
        f.write(hmac_sig)

    print(f"补丁编译完成: {output_path} ({total_size} bytes, v{patch_version})")
    if verbose:
        patch_hash = hashlib.sha256(patch_body).hexdigest()
        print(f"  SHA256: {patch_hash}")


def main():
    parser = argparse.ArgumentParser(description="规则补丁编译器 — YAML → .rulepatch")
    parser.add_argument("--input", required=True, help="变更描述 YAML 文件路径")
    parser.add_argument("--output", required=True, help="输出 .rulepatch 文件路径")
    parser.add_argument("--version", type=int, required=True, help="补丁版本号 (单调递增)")
    parser.add_argument("--verbose", action="store_true", help="详细输出")
    args = parser.parse_args()

    if not os.path.exists(args.input):
        print(f"错误: 输入文件不存在: {args.input}", file=sys.stderr)
        sys.exit(1)

    compile_patch(args.input, args.output, args.version, args.verbose)


if __name__ == "__main__":
    main()
