#!/usr/bin/env python3
"""
传导链事件分类器 (TF-IDF + 逻辑回归, 零深度学习依赖)
======================================================
2,218条 → 3分类: supply_disruption / trade_restriction / inventory_build
CPU 2秒, 中英文通用, 导出 pickle 模型供 C++/Python 推理
"""

import json, os, sys, io, pickle, random, math
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
import numpy as np
from collections import Counter
from scipy.sparse import csr_matrix, vstack
from scipy.special import softmax

TRAIN_FILE = os.path.join(os.path.dirname(__file__), "commodity_event_train.jsonl")
OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "commodity_event_model")
os.makedirs(OUTPUT_DIR, exist_ok=True)

LABELS = ["supply_disruption", "trade_restriction", "inventory_build"]
label2id = {l: i for i, l in enumerate(LABELS)}
id2label = {i: l for l, i in label2id.items()}


# ═══════════════════════════════════════
# TF-IDF 向量化 (纯手写, 零依赖)
# ═══════════════════════════════════════

class TfidfVectorizer:
    """字符n-gram TF-IDF (中英文通用, 无需分词)"""

    def __init__(self, ngram_range=(1, 3), max_features=3000, min_df=3):
        self.ngram_range = ngram_range
        self.max_features = max_features
        self.min_df = min_df
        self.vocab = {}       # ngram → index
        self.idf = None       # IDF 向量

    def _ngrams(self, text, n):
        text = text.lower()
        return [text[i:i+n] for i in range(len(text) - n + 1)]

    def fit(self, texts):
        # 统计 DF
        df = Counter()
        for t in texts:
            seen = set()
            for n in range(self.ngram_range[0], self.ngram_range[1] + 1):
                for ng in self._ngrams(t, n):
                    if ng not in seen:
                        df[ng] += 1
                        seen.add(ng)

        # 过滤 + Top-K
        filtered = [(k, v) for k, v in df.items()
                     if v >= self.min_df and len(k) >= 2]
        filtered.sort(key=lambda x: -x[1])
        self.vocab = {k: i for i, (k, _) in enumerate(filtered[:self.max_features])}

        # IDF
        N = len(texts)
        self.idf = np.ones(len(self.vocab))
        for ng, idx in self.vocab.items():
            self.idf[idx] = math.log((1 + N) / (1 + df.get(ng, 0))) + 1.0

        return self

    def transform(self, texts):
        rows, cols, data = [], [], []
        for i, t in enumerate(texts):
            tf = Counter()
            for n in range(self.ngram_range[0], self.ngram_range[1] + 1):
                for ng in self._ngrams(t, n):
                    if ng in self.vocab:
                        tf[self.vocab[ng]] += 1
            if tf:
                max_tf = max(tf.values())
                for idx, cnt in tf.items():
                    rows.append(i)
                    cols.append(idx)
                    data.append((cnt / max_tf) * self.idf[idx])
        return csr_matrix((data, (rows, cols)), shape=(len(texts), len(self.vocab)))

    def fit_transform(self, texts):
        return self.fit(texts).transform(texts)


# ═══════════════════════════════════════
# 逻辑回归 (SGD, 纯手写)
# ═══════════════════════════════════════

class LogisticRegression:
    def __init__(self, lr=0.1, epochs=50, l2=0.01):
        self.lr = lr
        self.epochs = epochs
        self.l2 = l2
        self.W = None  # (n_classes, n_features)
        self.b = None  # (n_classes,)

    def fit(self, X, y, X_val=None, y_val=None):
        n_classes = len(set(y))
        n_samples, n_features = X.shape
        self.W = np.random.randn(n_classes, n_features) * 0.01
        self.b = np.zeros(n_classes)

        # 转 one-hot
        Y = np.zeros((n_samples, n_classes))
        Y[np.arange(n_samples), y] = 1

        for epoch in range(self.epochs):
            # Mini-batch SGD
            perm = np.random.permutation(n_samples)
            total_loss = 0
            for start in range(0, n_samples, 64):
                idx = perm[start:start + 64]
                Xb = X[idx].toarray()
                yb = Y[idx]

                scores = Xb @ self.W.T + self.b
                probs = softmax(scores, axis=1)
                err = probs - yb

                grad_W = err.T @ Xb / len(idx) + self.l2 * self.W
                grad_b = err.mean(axis=0)

                self.W -= self.lr * grad_W
                self.b -= self.lr * grad_b

                total_loss += -np.sum(yb * np.log(probs + 1e-10)) / len(idx)

            if (epoch + 1) % 10 == 0 and X_val is not None:
                acc = self.score(X_val, y_val)
                print(f"  epoch {epoch+1:3d} | loss={total_loss:.3f} | val_acc={acc:.3f}")

        return self

    def predict(self, X):
        scores = X @ self.W.T + self.b
        return np.argmax(scores, axis=1)

    def predict_proba(self, X):
        scores = X @ self.W.T + self.b
        return softmax(scores, axis=1)

    def score(self, X, y):
        return np.mean(self.predict(X) == y)


def load_data():
    texts, labels = [], []
    with open(TRAIN_FILE, "r", encoding="utf-8") as f:
        for line in f:
            obj = json.loads(line.strip())
            texts.append(obj["text"])
            labels.append(label2id[obj["label"]])
    return texts, labels


def split_stratified(texts, labels, ratio=0.8):
    buckets = {i: [] for i in range(len(LABELS))}
    for t, l in zip(texts, labels):
        buckets[l].append((t, l))
    train_t, train_l = [], []
    val_t, val_l = [], []
    for l in range(len(LABELS)):
        items = buckets[l]
        random.shuffle(items)
        split = int(len(items) * ratio)
        for t, lab in items[:split]:
            train_t.append(t); train_l.append(lab)
        for t, lab in items[split:]:
            val_t.append(t); val_l.append(lab)
    return train_t, train_l, val_t, val_l


def metrics(y_true, y_pred):
    acc = np.mean(y_true == y_pred)
    print(f"  Accuracy: {acc:.3f}")
    for l in range(len(LABELS)):
        tp = np.sum((y_true == l) & (y_pred == l))
        fp = np.sum((y_true != l) & (y_pred == l))
        fn = np.sum((y_true == l) & (y_pred != l))
        prec = tp / (tp + fp) if (tp + fp) > 0 else 0
        rec = tp / (tp + fn) if (tp + fn) > 0 else 0
        f1 = 2 * prec * rec / (prec + rec) if (prec + rec) > 0 else 0
        n = np.sum(y_true == l)
        print(f"  {LABELS[l]:25s} P={prec:.3f} R={rec:.3f} F1={f1:.3f}  n={n}")
    return acc


def main():
    random.seed(42); np.random.seed(42)

    print("加载数据...")
    texts, labels = load_data()
    print(f"  {len(texts)} 条 | { {l: labels.count(i) for i,l in enumerate(LABELS)} }")

    train_t, train_l, val_t, val_l = split_stratified(texts, labels)
    print(f"  训练: {len(train_l)}  验证: {len(val_l)}")

    print("TF-IDF 向量化...")
    vec = TfidfVectorizer(max_features=3000)
    X_train = vec.fit_transform(train_t)
    X_val = vec.transform(val_t)
    print(f"  特征维度: {X_train.shape[1]}")

    print("训练逻辑回归...")
    model = LogisticRegression(lr=0.2, epochs=50, l2=0.001)
    model.fit(X_train, np.array(train_l), X_val, np.array(val_l))

    print("\n验证集评估:")
    preds = model.predict(X_val)
    acc = metrics(np.array(val_l), preds)

    # 保存
    with open(os.path.join(OUTPUT_DIR, "vectorizer.pkl"), "wb") as f:
        pickle.dump(vec, f)
    with open(os.path.join(OUTPUT_DIR, "classifier.pkl"), "wb") as f:
        pickle.dump(model, f)
    with open(os.path.join(OUTPUT_DIR, "labels.json"), "w") as f:
        json.dump({"label2id": label2id, "id2label": id2label}, f)

    print(f"\n模型已保存: {OUTPUT_DIR}")
    print(f"  大小: {os.path.getsize(os.path.join(OUTPUT_DIR,'vectorizer.pkl'))//1024}KB + "
          f"{os.path.getsize(os.path.join(OUTPUT_DIR,'classifier.pkl'))//1024}KB")


if __name__ == "__main__":
    main()
