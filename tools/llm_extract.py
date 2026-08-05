#!/usr/bin/env python3
"""
LLM结构化提取 — 拉取公司数据 + 调用Claude API提取业务段
=========================================================
Step 0: akshare/baostock → 公司概况文本
Step 1: Claude Haiku → JSON结构化业务段
Step 2: → llm_match + llm_direction + llm_import

用法:
  python llm_extract.py --stocks 601899.SH,002709.SZ          # 指定股票
  python llm_extract.py --index csi800 --limit 5               # 中证800前5只
  python llm_extract.py --dry-run                              # 只提取不导入
"""

import sys, os, io, json, time, re
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import anthropic
from db_config import pg_connect
from llm_match import CommodityMatcher
from llm_direction import compute_weight
from llm_import import Validator, ValidationResult

import logging
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("llm_extract")

# ── 加载Prompt ──
PROMPT_PATH = os.path.join(os.path.dirname(__file__), "llm_prompt.txt")
with open(PROMPT_PATH, "r", encoding="utf-8") as f:
    SYSTEM_PROMPT = f.read()


def get_company_info(symbol: str) -> dict:
    """拉取公司主营业务文本"""
    import akshare as ak
    import baostock as bs

    code = symbol.replace(".SH", "").replace(".SZ", "")
    info = {"symbol": symbol, "name": "", "business": "", "csrc": ""}

    # baostock: CSRC行业
    try:
        bs.login()
        rs = bs.query_stock_industry(code)
        if rs.error_code == '0':
            rows = []
            while rs.next():
                rows.append(rs.get_row_data())
            if rows:
                info["csrc"] = rows[0][3][:3]
        bs.logout()
    except Exception as e:
        logger.warning(f"baostock failed for {symbol}: {e}")

    # akshare: 公司概况
    try:
        df = ak.stock_individual_info_em(symbol=code)
        # df columns: item, value
        for _, row in df.iterrows():
            if row["item"] == "公司名称":
                info["name"] = str(row["value"])
            elif row["item"] == "主营构成":
                info["business"] += str(row["value"]) + "\n"
            elif row["item"] == "经营范围":
                info["business"] += str(row["value"]) + "\n"
    except Exception as e:
        logger.warning(f"akshare failed for {symbol}: {e}")

    # 如果akshare没有详细业务描述, 用baostock的行业名
    if not info["business"]:
        info["business"] = f"所属CSRC行业: {info.get('csrc', 'unknown')}"

    return info


def extract_segments(client: anthropic.Anthropic, company: dict) -> dict:
    """调用Claude提取业务段"""
    user_msg = f"""
Symbol: {company['symbol']}
Name: {company.get('name', 'unknown')}
Business Description:
{company.get('business', 'no data')[:2000]}
"""

    try:
        response = client.messages.create(
            model="claude-haiku-4-5-20251001",
            max_tokens=1024,
            temperature=0,
            system=SYSTEM_PROMPT,
            messages=[{"role": "user", "content": user_msg}],
        )

        text = response.content[0].text.strip()

        # 清理可能的 markdown code fences
        if text.startswith("```"):
            text = re.sub(r'^```\w*\n?', '', text)
            text = re.sub(r'\n?```$', '', text)

        result = json.loads(text)
        return result

    except json.JSONDecodeError as e:
        logger.warning(f"JSON parse failed for {company['symbol']}: {e}")
        logger.debug(f"Raw response: {text[:200]}")
        return {"symbol": company['symbol'], "mapped": False, "reason": f"JSON parse error: {e}"}
    except Exception as e:
        logger.error(f"API error for {company['symbol']}: {e}")
        return {"symbol": company['symbol'], "mapped": False, "reason": str(e)[:100]}


def process_results(company: dict, llm_result: dict,
                    matcher: CommodityMatcher, validator: Validator,
                    dry_run: bool) -> list:
    """将LLM输出转为ValidationResult列表"""
    results = []

    if llm_result.get("mapped") is False:
        logger.info(f"  {company['symbol']}: unmapped — {llm_result.get('reason', '')}")
        return results

    segments = llm_result.get("segments", [])
    overall_conf = llm_result.get("confidence", 0.5)

    for seg in segments:
        if seg.get("revenue_share", 0) < 0.10:
            continue  # 低于10%过滤

        product_name = seg.get("product", "")
        business_role = seg.get("business_role", "downstream_application")
        revenue_share = seg.get("revenue_share", 0.5)

        # 匹配product_id
        pid, match_conf, method = matcher.match(product_name)
        if pid is None:
            logger.info(f"  {company['symbol']}: '{product_name}' unmapped [{method}]")
            continue

        # 方向+敏感度
        w = compute_weight(business_role, revenue_share,
                          company.get("csrc", ""))

        confidence = min(overall_conf, match_conf)
        if match_conf < 0.7:
            confidence = match_conf

        vr = ValidationResult(
            symbol=company['symbol'],
            product_id=pid,
            direction=w["direction"],
            sensitivity=w["sensitivity"],
            confidence=confidence,
        )

        validator.validate(vr)
        results.append(vr)

        logger.info(f"  {company['symbol']}: {product_name} -> {pid} "
                    f"role={business_role} dir={w['direction']:+d} "
                    f"w={w['weight']:+.3f} [{method}] {vr.status}")

    return results


def run_batch(symbols: list, dry_run: bool = False, limit: int = None):
    """批量处理"""
    if limit:
        symbols = symbols[:limit]

    # API key: 从环境变量或claude config读取
    api_key = os.environ.get("ANTHROPIC_API_KEY")
    if not api_key:
        logger.error("需要设置 ANTHROPIC_API_KEY 环境变量")
        return

    client = anthropic.Anthropic(api_key=api_key)
    matcher = CommodityMatcher()
    conn = pg_connect()
    validator = Validator(conn)

    all_results = []
    n = 0

    for sym in symbols:
        n += 1
        logger.info(f"[{n}/{len(symbols)}] {sym}")

        company = get_company_info(sym)
        if not company.get("name"):
            company["name"] = sym

        llm_result = extract_segments(client, company)

        if llm_result:
            results = process_results(company, llm_result, matcher, validator, dry_run)
            all_results.extend(results)

        time.sleep(0.5)  # rate limiting

    # 导入
    if not dry_run and all_results:
        validator.import_results(all_results, version="llm_v1")
    else:
        logger.info(f"[DRY RUN] {len(all_results)}条结果 (未写入)")

    matcher.close()
    conn.close()

    return all_results


def get_csi800_symbols() -> list:
    """获取中证800成分股"""
    import akshare as ak
    df = ak.index_stock_cons(symbol="000906")
    codes = []
    for _, row in df.iterrows():
        code = str(row["品种代码"])
        if code.endswith(".SH") or code.endswith(".SZ"):
            codes.append(code)
        elif code.startswith("6"):
            codes.append(f"{code}.SH")
        else:
            codes.append(f"{code}.SZ")
    return codes


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--stocks", help="逗号分隔的股票代码")
    parser.add_argument("--index", choices=["csi800"], help="指数成分股")
    parser.add_argument("--limit", type=int, help="限制数量")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    if args.stocks:
        symbols = [s.strip() for s in args.stocks.split(",")]
    elif args.index == "csi800":
        symbols = get_csi800_symbols()
        logger.info(f"中证800: {len(symbols)} 只")
    else:
        # 默认: 基准10只
        symbols = [
            "601899.SH","002709.SZ","002460.SZ","600019.SH","002714.SZ",
            "600438.SH","600111.SH","603799.SH","600585.SH","000876.SZ",
        ]
        logger.info(f"默认10只基准股")

    run_batch(symbols, dry_run=args.dry_run, limit=args.limit)
