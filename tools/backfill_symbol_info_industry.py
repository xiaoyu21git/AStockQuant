from __future__ import annotations

import argparse
import json
import os
import requests
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Iterable, Optional

import akshare as ak
import pymysql


PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.a_share_symbol_utils import normalize_symbol


MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
    "autocommit": False,
}


def disable_proxy_env() -> None:
    for key in (
        "http_proxy",
        "https_proxy",
        "HTTP_PROXY",
        "HTTPS_PROXY",
        "all_proxy",
        "ALL_PROXY",
        "no_proxy",
        "NO_PROXY",
    ):
        os.environ.pop(key, None)
    os.environ["NO_PROXY"] = "*"
    os.environ["no_proxy"] = "*"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="回填 symbol_info.industry_code，并同步缓存集里的 industry_code")
    parser.add_argument("--missing-only", action="store_true", default=True, help="仅回填 industry_code 为空的股票")
    parser.add_argument("--all", dest="missing_only", action="store_false", help="重刷全部股票的 industry_code")
    parser.add_argument("--workers", type=int, default=1, help="并发数，默认 1；cninfo 行业接口线程不安全")
    parser.add_argument("--retries", type=int, default=3, help="单只股票拉取重试次数，默认 3")
    parser.add_argument("--limit", type=int, default=0, help="仅处理前 N 只股票，0 表示不限")
    parser.add_argument("--batch-size", type=int, default=100, help="数据库批量提交大小，默认 100")
    parser.add_argument("--dataset-id", action="append", type=int, default=[], help="同步指定缓存集的 industry_code，可重复传入")
    parser.add_argument("--symbols-from-dataset", action="store_true", help="仅处理指定缓存集里出现的股票")
    parser.add_argument(
        "--dataset-root",
        default=str(Path(os.environ.get("LOCALAPPDATA", "")) / "astockquantapp-exe" / "datasets"),
        help="缓存集目录，默认 %%LOCALAPPDATA%%/astockquantapp-exe/datasets",
    )
    parser.add_argument("--dry-run", action="store_true", help="仅输出将发生的变更，不写数据库和文件")
    return parser.parse_args()


def get_connection():
    return pymysql.connect(**MYSQL_CONFIG)


def chunked(values: list[tuple[str, str]], chunk_size: int) -> Iterable[list[tuple[str, str]]]:
    for index in range(0, len(values), chunk_size):
        yield values[index:index + chunk_size]


def load_target_symbols(missing_only: bool, limit: int) -> list[str]:
    sql = [
        "SELECT symbol",
        "FROM symbol_info",
        "WHERE asset_class = 'STOCK'",
        "AND status <> 'DELISTED'",
    ]
    if missing_only:
        sql.append("AND (industry_code IS NULL OR TRIM(industry_code) = '')")
    sql.append("ORDER BY symbol")
    if limit > 0:
        sql.append(f"LIMIT {int(limit)}")

    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            cursor.execute("\n".join(sql))
            return [str(row[0]).strip() for row in cursor.fetchall() if row and row[0]]
    finally:
        conn.close()


def load_dataset_symbols(dataset_ids: list[int], dataset_root: Path) -> list[str]:
    symbols: set[str] = set()
    for dataset_id in dataset_ids:
        data_file = dataset_root / f"dataset_{dataset_id}_data.json"
        if not data_file.exists():
            raise FileNotFoundError(f"dataset file not found: {data_file}")
        data = json.loads(data_file.read_text(encoding="utf-8"))
        if not isinstance(data, list):
            raise RuntimeError(f"unexpected dataset payload: {data_file}")
        for row in data:
            if not isinstance(row, dict):
                continue
            symbol = normalize_symbol(str(row.get("symbol") or "").strip())
            if symbol:
                symbols.add(symbol)
    return sorted(symbols)


def load_target_symbols_from_candidates(symbols: list[str], missing_only: bool, limit: int) -> list[str]:
    normalized = [normalize_symbol(symbol) for symbol in symbols if str(symbol).strip()]
    if not normalized:
        return []

    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            selected: list[str] = []
            for batch in chunked([(symbol, "") for symbol in normalized], 500):
                batch_symbols = [item[0] for item in batch]
                placeholders = ",".join(["%s"] * len(batch_symbols))
                sql = [
                    "SELECT symbol",
                    "FROM symbol_info",
                    "WHERE asset_class = 'STOCK'",
                    "AND status <> 'DELISTED'",
                    f"AND symbol IN ({placeholders})",
                ]
                if missing_only:
                    sql.append("AND (industry_code IS NULL OR TRIM(industry_code) = '')")
                
                sql.append("ORDER BY symbol")
                cursor.execute("\n".join(sql), batch_symbols)
                selected.extend(str(row[0]).strip() for row in cursor.fetchall() if row and row[0])

            if limit > 0:
                selected = selected[:limit]
            return selected
    finally:
        conn.close()


def extract_industry_from_df(df) -> str:
    for _, row in df.iterrows():
        item = str(row.get("item") or "").strip()
        if item == "行业":
            return str(row.get("value") or "").strip()
    return ""


PREFERRED_CNINFO_STANDARDS = (
    "中证行业分类标准",
    "申银万国行业分类标准",
    "巨潮行业分类标准",
    "证监会行业分类标准（2012）",
    "新财富行业分类标准",
    "恒生行业分类",
)


def extract_cninfo_industry(df) -> str:
    if df is None or df.empty:
        return ""

    working = df.copy()
    for column in ("分类标准", "行业中类", "变更日期"):
        if column not in working.columns:
            return ""

    working["分类标准"] = working["分类标准"].astype(str).str.strip()
    working["行业中类"] = working["行业中类"].astype(str).str.strip()
    working["变更日期"] = working["变更日期"].astype(str).str.strip()

    for standard in PREFERRED_CNINFO_STANDARDS:
        matched = working[working["分类标准"] == standard]
        matched = matched[matched["行业中类"] != ""]
        if matched.empty:
            continue
        matched = matched.sort_values(by="变更日期")
        return str(matched.iloc[-1]["行业中类"]).strip()

    fallback = working[working["行业中类"] != ""]
    if fallback.empty:
        return ""
    fallback = fallback.sort_values(by="变更日期")
    return str(fallback.iloc[-1]["行业中类"]).strip()


def extract_cninfo_industry_from_records(records: list[dict]) -> str:
    normalized_rows: list[tuple[str, str, str]] = []
    for row in records:
        standard = str(row.get("F002V") or "").strip()
        industry = str(
            row.get("F007V")
            or row.get("F006V")
            or row.get("F005V")
            or row.get("F004V")
            or ""
        ).strip()
        vary_date = str(row.get("VARYDATE") or "").strip()
        if industry:
            normalized_rows.append((standard, industry, vary_date))

    if not normalized_rows:
        return ""

    for standard in PREFERRED_CNINFO_STANDARDS:
        matched = [row for row in normalized_rows if row[0] == standard]
        if matched:
            matched.sort(key=lambda item: item[2])
            return matched[-1][1]

    normalized_rows.sort(key=lambda item: item[2])
    return normalized_rows[-1][1]


def fetch_cninfo_records_raw(symbol: str) -> list[dict]:
    import akshare.stock.stock_industry_cninfo as cninfo_mod

    js_code = cninfo_mod.py_mini_racer.MiniRacer()
    js_code.eval(cninfo_mod._get_file_content_ths("cninfo.js"))
    mcode = js_code.call("getResCode1")
    headers = {
        "Accept": "*/*",
        "Accept-Encoding": "gzip, deflate",
        "Accept-Language": "zh-CN,zh;q=0.9,en;q=0.8",
        "Cache-Control": "no-cache",
        "Content-Length": "0",
        "Host": "webapi.cninfo.com.cn",
        "Accept-Enckey": mcode,
        "Origin": "https://webapi.cninfo.com.cn",
        "Pragma": "no-cache",
        "Proxy-Connection": "keep-alive",
        "Referer": "https://webapi.cninfo.com.cn/",
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/93.0.4577.63 Safari/537.36",
        "X-Requested-With": "XMLHttpRequest",
    }
    params = {
        "scode": symbol,
        "sdate": "2009-12-27",
        "edate": "2026-05-15",
    }
    response = requests.post(
        "https://webapi.cninfo.com.cn/api/stock/p_stock2110",
        params=params,
        headers=headers,
        timeout=20,
    )
    response.raise_for_status()
    payload = response.json()
    return list(payload.get("records") or [])


def fetch_symbol_industry(symbol: str, retries: int) -> tuple[str, str]:
    normalized = normalize_symbol(symbol)
    code = normalized.split(".", 1)[0]
    last_error: Optional[Exception] = None

    for attempt in range(1, retries + 1):
        try:
            df = ak.stock_industry_change_cninfo(symbol=code)
            industry = extract_cninfo_industry(df)
            return normalized, industry
        except KeyError as exc:
            if str(exc).strip("'") == "变更日期":
                try:
                    records = fetch_cninfo_records_raw(code)
                    industry = extract_cninfo_industry_from_records(records)
                    if industry:
                        return normalized, industry
                except Exception as raw_exc:
                    last_error = raw_exc
                else:
                    last_error = exc
            else:
                last_error = exc
        except Exception as exc:
            last_error = exc
            if attempt < retries:
                time.sleep(min(0.5 * attempt, 2.0))

    raise RuntimeError(f"{normalized}: failed to fetch industry after {retries} attempts: {last_error}")


def load_existing_industry_map(symbols: Iterable[str]) -> dict[str, str]:
    values = [normalize_symbol(symbol) for symbol in symbols if str(symbol).strip()]
    if not values:
        return {}

    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            placeholders = ",".join(["%s"] * len(values))
            cursor.execute(
                f"SELECT symbol, TRIM(COALESCE(industry_code, '')) FROM symbol_info WHERE symbol IN ({placeholders})",
                values,
            )
            return {str(symbol).strip(): str(industry or "").strip() for symbol, industry in cursor.fetchall()}
    finally:
        conn.close()


def write_industry_updates(assignments: list[tuple[str, str]], dry_run: bool, batch_size: int) -> int:
    if not assignments:
        return 0
    if dry_run:
        return len(assignments)

    conn = get_connection()
    try:
        with conn.cursor() as cursor:
            for batch in chunked(assignments, batch_size):
                cursor.executemany(
                    "UPDATE symbol_info SET industry_code = %s WHERE symbol = %s",
                    [(industry, symbol) for symbol, industry in batch],
                )
        conn.commit()
        return len(assignments)
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


def refresh_dataset_industry_codes(dataset_id: int, dataset_root: Path, dry_run: bool) -> tuple[int, int]:
    data_file = dataset_root / f"dataset_{dataset_id}_data.json"
    if not data_file.exists():
        raise FileNotFoundError(f"dataset file not found: {data_file}")

    raw = data_file.read_text(encoding="utf-8")
    data = json.loads(raw)
    if not isinstance(data, list):
        raise RuntimeError(f"unexpected dataset payload: {data_file}")

    symbols = []
    for row in data:
        if not isinstance(row, dict):
            continue
        symbol = normalize_symbol(str(row.get("symbol") or "").strip())
        if symbol:
            symbols.append(symbol)

    industry_by_symbol = load_existing_industry_map(symbols)
    changed_rows = 0
    covered_rows = 0

    for row in data:
        if not isinstance(row, dict):
            continue
        symbol = normalize_symbol(str(row.get("symbol") or "").strip())
        industry = industry_by_symbol.get(symbol, "")
        if not industry:
            continue
        covered_rows += 1
        current = str(row.get("industry_code") or "").strip()
        if current == industry:
            continue
        row["industry_code"] = industry
        changed_rows += 1

    if changed_rows and not dry_run:
        data_file.write_text(json.dumps(data, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")

    return changed_rows, covered_rows


def main() -> None:
    disable_proxy_env()
    args = parse_args()
    dataset_root = Path(args.dataset_root)

    if args.symbols_from_dataset:
        if not args.dataset_id:
            raise RuntimeError("--symbols-from-dataset requires at least one --dataset-id")
        dataset_symbols = load_dataset_symbols(args.dataset_id, dataset_root)
        targets = load_target_symbols_from_candidates(dataset_symbols, args.missing_only, args.limit)
    else:
        targets = load_target_symbols(args.missing_only, args.limit)
    print(f"targets={len(targets)} missing_only={args.missing_only} workers={args.workers} dry_run={args.dry_run}")
    if not targets:
        print("no target symbols")
    
    assignments: list[tuple[str, str]] = []
    failed: list[str] = []

    if targets:
        worker_count = 1
        if args.workers != 1:
            print(f"forcing workers=1 because cninfo industry endpoint is not thread-safe (requested={args.workers})")
        with ThreadPoolExecutor(max_workers=worker_count) as executor:
            future_map = {executor.submit(fetch_symbol_industry, symbol, args.retries): symbol for symbol in targets}
            for index, future in enumerate(as_completed(future_map), start=1):
                symbol = future_map[future]
                try:
                    normalized, industry = future.result()
                    if industry:
                        assignments.append((normalized, industry))
                    else:
                        failed.append(f"{normalized}:empty")
                except Exception as exc:
                    failed.append(f"{symbol}:{exc}")

                if index == len(targets) or index % 100 == 0:
                    print(
                        f"progress={index}/{len(targets)} assignments={len(assignments)} failures={len(failed)}",
                        flush=True,
                    )

    written = write_industry_updates(assignments, args.dry_run, max(1, args.batch_size))
    print(f"db_updates={written} failures={len(failed)}")
    for sample in assignments[:10]:
        print(f"sample_update {sample[0]} -> {sample[1]}")
    for sample in failed[:20]:
        print(f"sample_failure {sample}")

    for dataset_id in args.dataset_id:
        changed_rows, covered_rows = refresh_dataset_industry_codes(dataset_id, dataset_root, args.dry_run)
        print(
            f"dataset_id={dataset_id} changed_rows={changed_rows} covered_rows={covered_rows} root={dataset_root}",
            flush=True,
        )


if __name__ == "__main__":
    main()