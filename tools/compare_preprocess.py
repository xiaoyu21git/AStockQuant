#!/usr/bin/env python3
"""
对比 C++ 和 Python 的预处理是否一致。
用法: python tools/compare_preprocess.py --data <arrow> --scaler models/dl_v4/scaler.json
"""
import argparse, json, os, sys
import numpy as np
import pyarrow as pa, pyarrow.ipc as ipc

FEATURE_FIELDS = [
    "close", "open", "high", "low",
    "volume", "turnover_rate", "amplitude",
    "pe_ratio", "pb_ratio", "market_cap",
    "roe", "industry_code",
]
LOOKBACK = 20
W_EXTEND = 100  # C++ FeatureTensorBuilder: 5 * W = 100


def load_data(arrow_path, fields):
    f = pa.memory_map(str(arrow_path), "rb")
    reader = ipc.open_file(f)
    sym_data = {}
    for bi in range(reader.num_record_batches):
        batch = reader.get_batch(bi)
        t = pa.Table.from_batches([batch])
        sc = t.column("symbol").to_pylist()
        dc = t.column("trade_date").to_pylist()
        fc = {fn: t.column(fn).to_pylist() for fn in fields if fn in t.column_names}
        for ri in range(t.num_rows):
            s = sc[ri]
            row = {}
            for fn in fields:
                if fn in fc:
                    v = fc[fn][ri]
                    row[fn] = float(v) if v is not None and v == v else float("nan")
            sym_data.setdefault(s, {})[str(dc[ri])[:10]] = row
    return sym_data


def preprocess_python(rows, fields, anchor_date, lookback, scaler):
    """训练侧: 全历史 ffill → 切片, 完全匹配训练逻辑"""
    dates = sorted(rows.keys())
    n, nf = len(dates), len(fields)
    feat = np.full((n, nf), np.nan, dtype=np.float32)
    for i, d in enumerate(dates):
        for j, fn in enumerate(fields):
            v = rows[d].get(fn, np.nan)
            if np.isfinite(v): feat[i, j] = v

    # 全历史 ffill (向量化)
    mask = np.isfinite(feat)
    idx = np.where(mask, np.arange(n)[:, None], 0)
    np.maximum.accumulate(idx, axis=0, out=idx)
    feat = feat[idx, np.arange(nf)]

    if anchor_date not in dates: return None
    anchor_idx = dates.index(anchor_date)
    if anchor_idx < lookback: return None
    window = feat[anchor_idx - lookback:anchor_idx]

    if np.any(np.isnan(window)): return None

    means = np.array(scaler["mean"], dtype=np.float64)
    scales = np.array(scaler["scale"], dtype=np.float64) + 1e-12
    return (window.astype(np.float64) - means) / scales


def preprocess_cpp(rows, fields, anchor_date, lookback, scaler, extend=100):
    """C++ 侧: 扩展窗口 ffill → 截尾 → 归一化, 模拟 FeatureTensorBuilder"""
    dates = sorted(rows.keys())
    n, nf = len(dates), len(fields)

    # 构建全量特征矩阵
    feat = np.full((n, nf), np.nan, dtype=np.float32)
    for i, d in enumerate(dates):
        for j, fn in enumerate(fields):
            v = rows[d].get(fn, np.nan)
            if np.isfinite(v): feat[i, j] = v

    if anchor_date not in dates: return None
    anchor_idx = dates.index(anchor_date)

    # 扩展窗口起始: anchor - extend
    ext_start = max(0, anchor_idx - extend)
    ext_end = anchor_idx + 1  # 包含 anchorDate (后续丢弃)
    ext_feat = feat[ext_start:ext_end].copy()
    ext_dates = dates[ext_start:ext_end]

    if ext_feat.shape[0] < lookback + 1:
        print(f"  [cpp] 数据不足: 扩展窗口{ext_feat.shape[0]}天 < {lookback+1}")
        return None

    # ffill 仅在扩展窗口内 (逐列)
    for j in range(nf):
        col = ext_feat[:, j]
        last_valid = np.nan
        for i in range(len(col)):
            if np.isfinite(col[i]):
                last_valid = col[i]
            elif np.isfinite(last_valid):
                col[i] = last_valid

    # 截取尾部 W+1 → 丢弃最后 1 天 → 得到 W 天
    tail = ext_feat[-(lookback + 1):]
    window = tail[:-1]  # 丢弃 anchorDate

    if np.any(np.isnan(window)):
        print(f"  [cpp] ffill后仍有NaN")
        return None

    means = np.array(scaler["mean"], dtype=np.float64)
    scales = np.array(scaler["scale"], dtype=np.float64) + 1e-12
    return (window.astype(np.float64) - means) / scales


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--data", required=True, help="Arrow 缓存路径")
    p.add_argument("--scaler", required=True, help="scaler.json 路径")
    p.add_argument("--date", default="2024-01-10", help="锚定日期")
    p.add_argument("--symbols", type=int, default=5, help="对比标的数")
    args = p.parse_args()

    with open(args.scaler) as f:
        scaler = json.load(f)
    fields = FEATURE_FIELDS[:len(scaler["mean"])]

    sym_data = load_data(args.data, fields)

    # 自动找有效日期
    all_dates = set()
    for s in sym_data:
        all_dates.update(sym_data[s].keys())
    sorted_dates = sorted(all_dates)
    test_date = args.date if args.date in sorted_dates else sorted_dates[120]
    print(f"测试日期: {test_date}")
    print(f"{'标的':<14} {'特征':<16} {'Python mean':>12} {'C++ mean':>12} {'差异':>12} {'一致?':>8}")
    print("-" * 80)

    matched = 0
    total = 0
    for s in sorted(sym_data.keys()):
        w_py = preprocess_python(sym_data[s], fields, test_date, LOOKBACK, scaler)
        w_cpp = preprocess_cpp(sym_data[s], fields, test_date, LOOKBACK, scaler, W_EXTEND)
        if w_py is None and w_cpp is None:
            continue
        total += 1
        if w_py is None or w_cpp is None:
            status = "[WARN] 仅一侧有效"
            if w_py is None: status = "[WARN] C++有效/Python无效"
            if w_cpp is None: status = "[WARN] Python有效/C++无效"
            print(f"{s:<14} {'ALL':<16} {'-':>12} {'-':>12} {'-':>12} {status:>8}")
            continue

        all_match = True
        for j, fn in enumerate(fields):
            diff = np.max(np.abs(w_py[:, j] - w_cpp[:, j]))
            match = "OK" if diff < 1e-4 else "XX"
            if diff >= 1e-4:
                all_match = False
            if total <= args.symbols:
                pm = np.mean(w_py[:, j])
                cm = np.mean(w_cpp[:, j])
                print(f"{s if j==0 else '':<14} {fn:<16} {pm:>12.6f} {cm:>12.6f} {diff:>12.2e} {match:>8}")

        if all_match:
            matched += 1
        if total >= args.symbols:
            break

    if total > 0:
        print(f"\n结果: {matched}/{total} 只标的预处理完全一致")
        if matched == total:
            print("OK C++ 和 Python 预处理完全一致，输入张量相同")
        else:
            print(f"[WARN]  {total - matched} 只标的存在差异，C++ 预处理与训练不一致")


if __name__ == "__main__":
    main()
