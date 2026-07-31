#!/usr/bin/env python3
"""
验证 ONNX 模型与 PyTorch 模型推理是否一致。
用法: python tools/verify_onnx.py --model models/dl_v4 --data <arrow_path> --date 2024-01-10
"""
import argparse, json, os, sys
import numpy as np
import pyarrow as pa, pyarrow.ipc as ipc
import torch
import onnxruntime as ort

FEATURE_FIELDS = [
    "close", "open", "high", "low",
    "volume", "turnover_rate", "amplitude",
    "pe_ratio", "pb_ratio", "market_cap",
    "roe", "industry_code",
]
LOOKBACK = 20


def load_scaler(model_dir):
    with open(os.path.join(model_dir, "scaler.json")) as f:
        return json.load(f)


def load_feature_config(model_dir):
    with open(os.path.join(model_dir, "feature_config.json")) as f:
        return json.load(f)


def infer_hidden_size(state_dict):
    """从 checkpoint 推断 hidden_size: lstm.weight_ih_l0 形状 [4*H, F]"""
    w = state_dict["lstm.weight_ih_l0"]
    return w.shape[0] // 4


def infer_num_layers(state_dict):
    """从 checkpoint 推断 num_layers: 数 lstm.weight_ih_l{N}"""
    n = 0
    while f"lstm.weight_ih_l{n}" in state_dict:
        n += 1
    return n


class FactorLSTM(torch.nn.Module):
    def __init__(self, n_features, hidden_size=128, num_layers=2, dropout=0.2):
        super().__init__()
        self.lstm = torch.nn.LSTM(n_features, hidden_size, num_layers,
                                   batch_first=True, dropout=dropout)
        self.fc = torch.nn.Linear(hidden_size, 1)

    def forward(self, x):
        out, _ = self.lstm(x)
        return self.fc(out[:, -1, :])


def build_window(rows, fields, anchor_date, lookback, scaler):
    """用训练逻辑构建一个标的的窗口（对齐 C++ FeatureTensorBuilder）"""
    dates = sorted(rows.keys())
    n, nf = len(dates), len(fields)
    feat = np.full((n, nf), np.nan, dtype=np.float32)
    for i, d in enumerate(dates):
        for j, fn in enumerate(fields):
            v = rows[d].get(fn, np.nan)
            if np.isfinite(v): feat[i, j] = v

    # ffill — 全历史（与训练一致）
    mask = np.isfinite(feat)
    idx = np.where(mask, np.arange(n)[:, None], 0)
    np.maximum.accumulate(idx, axis=0, out=idx)
    feat = feat[idx, np.arange(nf)]

    # 找到 anchor_date 索引
    if anchor_date not in dates:
        return None
    anchor_idx = dates.index(anchor_date)
    if anchor_idx < lookback: return None
    window = feat[anchor_idx - lookback:anchor_idx]  # [anchor-lookback : anchor-1]

    if np.any(np.isnan(window)): return None

    # 用训练保存的 scaler 做归一化（无裁剪，与 sklearn 一致）
    means = np.array(scaler["mean"], dtype=np.float64)
    scales = np.array(scaler["scale"], dtype=np.float64) + 1e-12
    window_norm = (window.astype(np.float64) - means) / scales
    return window_norm.astype(np.float32)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--model", required=True, help="模型目录 (含 model.onnx, best_model.pt, scaler.json)")
    p.add_argument("--data", required=True, help="Arrow 缓存路径")
    p.add_argument("--date", default="", help="锚定日期 YYYY-MM-DD (不指定则自动选择)")
    p.add_argument("--symbols", type=int, default=20, help="对比标的数")
    args = p.parse_args()

    scaler = load_scaler(args.model)
    config = load_feature_config(args.model)
    fields = config.get("fields", FEATURE_FIELDS)

    # ── 加载 PyTorch 模型 ──
    pt_path = os.path.join(args.model, "best_model.pt")
    state_dict = torch.load(pt_path, weights_only=True, map_location='cpu')
    hidden_size = infer_hidden_size(state_dict)
    num_layers = infer_num_layers(state_dict)
    pt_model = FactorLSTM(len(fields), hidden_size, num_layers)
    pt_model.load_state_dict(state_dict)
    pt_model.eval()
    print(f"[verify] PyTorch模型: hidden_size={hidden_size} layers={num_layers}")

    # ── 加载 ONNX 模型 ──
    onnx_path = os.path.join(args.model, "model.onnx")
    sess = ort.InferenceSession(onnx_path)

    # ── 加载数据 ──
    f = pa.memory_map(str(args.data), "rb")
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

    # ── 找第一个有效的交易日期 ──
    all_dates = set()
    for s in sym_data:
        all_dates.update(sym_data[s].keys())
    sorted_dates = sorted(all_dates)
    # 跳过前 120 天保证有足够回溯
    test_date = args.date if args.date in sorted_dates else None
    if not test_date:
        for d in sorted_dates:
            if d >= "2024-01-01" and d in sorted_dates:
                test_date = d
                break
    if not test_date:
        test_date = sorted_dates[120] if len(sorted_dates) > 120 else sorted_dates[-1]
    print(f"[verify] 测试日期: {test_date}")
    valid = 0
    diffs = []
    for s in sorted(sym_data.keys()):
        window = build_window(sym_data[s], fields, test_date, LOOKBACK, scaler)
        if window is None: continue

        x_pt = torch.FloatTensor(window).unsqueeze(0)  # [1, W, F]
        with torch.no_grad():
            out_pt = pt_model(x_pt).item()

        x_onnx = window.reshape(1, LOOKBACK, len(fields)).astype(np.float32)
        out_onnx = sess.run(None, {"input": x_onnx})[0].flatten()[0]

        diff = abs(out_pt - out_onnx)
        diffs.append(diff)
        valid += 1
        if valid <= 5:
            print(f"  {s}: pt={out_pt:.6f}  onnx={out_onnx:.6f}  diff={diff:.2e}")
        if valid >= args.symbols: break

    if diffs:
        print(f"\n共对比 {len(diffs)} 只标的")
        print(f"最大差异: {max(diffs):.4e}")
        print(f"平均差异: {np.mean(diffs):.4e}")

        # Spearman rank — PyTorch vs ONNX 排序是否一致
        from scipy.stats import spearmanr
        # 需要收集一批输出做对比
        all_pt, all_onnx = [], []
        count = 0
        for s in sorted(sym_data.keys()):
            window = build_window(sym_data[s], fields, test_date, LOOKBACK, scaler)
            if window is None: continue
            x_pt = torch.FloatTensor(window).unsqueeze(0)
            with torch.no_grad(): all_pt.append(pt_model(x_pt).item())
            x_onnx = window.reshape(1, LOOKBACK, len(fields)).astype(np.float32)
            all_onnx.append(sess.run(None, {"input": x_onnx})[0].flatten()[0])
            count += 1
            if count >= 500: break

        if len(all_pt) >= 5:
            r, p = spearmanr(all_pt, all_onnx)
            print(f"PyTorch vs ONNX 排序 Spearman R: {r:.6f} (p={p:.6f})")
            if r < 0.999:
                print("⚠️  ONNX 与 PyTorch 输出不一致！检查 ONNX 导出或 ONNX Runtime 版本。")
            else:
                print("✓  ONNX 与 PyTorch 完全一致。")
    else:
        print(f"⚠️  {test_date} 没有可用的标的数据")


if __name__ == "__main__":
    main()
