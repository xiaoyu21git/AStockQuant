"""
run_daily_update_pipeline.py
串联执行日线更新、缺失字段回填与收口校验，适合作为一键入口或计划任务目标。
"""

from __future__ import annotations

import argparse
import bisect
import datetime as dt
import json
import os
import subprocess
import sys
from pathlib import Path

import psycopg2

PROJECT_ROOT = Path(__file__).resolve().parents[1]

if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.a_share_symbol_utils import is_supported_akshare_stock_symbol
from tools.history_start_policy import clamp_history_start_date
from tools.trading_day_utils import DEFAULT_MARKET_CLOSE_TIME, get_trade_calendar, parse_time_text, resolve_latest_closed_trade_date


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from db_config import PG_CONFIG

MYSQL_CONFIG = PG_CONFIG  # 兼容旧变量名

DEFAULT_AUTO_CLOSE_TIME = "15:40"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="一键执行最新日线对齐、可选历史补数、可选财务补数以及缺失字段回填与收口校验"
    )
    parser.add_argument(
        "--interactive",
        action="store_true",
        help="进入简单交互菜单，按提示选择更新方案",
    )
    parser.add_argument(
        "--target-date",
        help="指定目标交易日，格式 YYYY-MM-DD；未提供时自动解析最近已收盘交易日",
    )
    parser.add_argument(
        "--close-time",
        help="收盘判断时间，格式 HH:MM；会同时传递给 update 和 verify",
    )
    parser.add_argument(
        "--wait-until-close",
        action="store_true",
        help="若尚未到达下一次收盘时间，则等待到收盘后再执行更新",
    )
    parser.add_argument(
        "--daily-close-profile",
        action="store_true",
        help="应用推荐的日终自动更新配置：默认 close-time=15:40，包含 latest/history 与所有日线相关回填，并允许可选步骤失败后继续执行",
    )
    parser.add_argument(
        "--continue-on-step-failure",
        action="store_true",
        help="可选步骤失败后继续执行剩余步骤，并在结束时输出汇总；latest update 与 verify 仍视为硬失败",
    )
    parser.add_argument(
        "--report-file",
        default="",
        help="将执行摘要写入 JSON 文件，适合计划任务或外部调度系统收集",
    )
    parser.add_argument(
        "--sample-limit",
        type=int,
        default=20,
        help="校验阶段输出的落后股票样本上限，默认 20",
    )
    parser.add_argument(
        "--include-history-gaps",
        action="store_true",
        help="在完成最新数据对齐后，再追加补历史内部缺口",
    )
    parser.add_argument(
        "--with-financial",
        action="store_true",
        help="在日线更新之后顺带执行财务历史回填",
    )
    parser.add_argument(
        "--financial-limit",
        type=int,
        default=10000,
        help="财务历史回填单次查询返回上限，默认 10000",
    )
    parser.add_argument(
        "--financial-workers",
        type=int,
        default=8,
        help="财务历史回填并发线程数，默认 8",
    )
    parser.add_argument(
        "--skip-derived-backfill",
        action="store_true",
        help="跳过 change_pct/change_amt/amplitude 派生字段回填",
    )
    parser.add_argument(
        "--skip-valuation-backfill",
        action="store_true",
        help="跳过 AK 估值回填",
    )
    parser.add_argument(
        "--skip-caps-backfill",
        action="store_true",
        help="跳过基于已有股本快照的市值补全",
    )
    parser.add_argument(
        "--skip-turnover-backfill",
        action="store_true",
        help="跳过 turnover_rate 回填",
    )
    parser.add_argument(
        "--valuation-limit-symbols",
        type=int,
        default=0,
        help="限制估值回填股票数量，默认 0 表示不限制",
    )
    parser.add_argument(
        "--valuation-sleep",
        type=float,
        default=0.15,
        help="估值回填每只股票之间的休眠秒数，默认 0.15",
    )
    parser.add_argument(
        "--baostock-only",
        action="store_true",
        help="仅运行 Baostock 单线程全量更新 (跳过所有回填步骤)",
    )
    return parser.parse_args()


def should_run_interactive(raw_argv: list[str]) -> bool:
    if "--interactive" in raw_argv:
        return True
    return not raw_argv and sys.stdin.isatty() and sys.stdout.isatty()


def prompt_text(prompt: str, default: str = "") -> str:
    suffix = f" [{default}]" if default else ""
    value = input(f"{prompt}{suffix}: ").strip()
    return value or default


def prompt_yes_no(prompt: str, default: bool) -> bool:
    default_text = "Y/n" if default else "y/N"
    value = input(f"{prompt} [{default_text}]: ").strip().lower()
    if not value:
        return default
    return value in {"y", "yes", "1", "true"}


def prompt_menu_choice() -> str:
    choices = {
        "1": "最新行情更新 (日线+派生字段+周月线+分钟线+校验)",
        "2": "最新行情 + 历史缺口回填",
        "3": "仅历史缺口回填",
    }
    print("\n可执行方案:")
    for key, label in choices.items():
        print(f"  {key}. {label}")

    while True:
        choice = input("请选择方案 [1]: ").strip() or "1"
        if choice in choices:
            print(f"已选择: {choices[choice]}")
            return choice
        print("无效选择，请输入 1-3")


def apply_interactive_profile(args: argparse.Namespace) -> argparse.Namespace | None:
    print("数据更新交互模式")
    print("最新行情: 日线更新 → 派生字段 → 估值/市值/换手率 → 周月线聚合 → 校验")
    print("历史补缺: 回填日线历史缺口 → 派生字段历史缺口")

    choice = prompt_menu_choice()
    if choice == "1":
        args.include_history_gaps = False
    elif choice == "2":
        args.include_history_gaps = True
    elif choice == "3":
        args.include_history_gaps = True
        args.skip_derived_backfill = True
        args.skip_valuation_backfill = True
        args.skip_caps_backfill = True
        args.skip_turnover_backfill = True

    while True:
        target_date_text = prompt_text("目标交易日，回车自动识别最近已收盘交易日", args.target_date or "")
        if not target_date_text:
            args.target_date = None
            break
        try:
            dt.date.fromisoformat(target_date_text)
            args.target_date = target_date_text
            break
        except ValueError:
            print("日期格式错误，请输入 YYYY-MM-DD")

    print("\n即将执行:")
    print(f"  target_date={args.target_date or 'auto'}")
    print(f"  include_history_gaps={args.include_history_gaps}")
    print(f"  with_financial={args.with_financial}")

    if not prompt_yes_no("确认开始执行", True):
        print("已取消执行")
        return None

    return args


def apply_daily_close_profile(args: argparse.Namespace) -> argparse.Namespace:
    args.include_history_gaps = True
    args.continue_on_step_failure = True
    if not args.close_time:
        args.close_time = DEFAULT_AUTO_CLOSE_TIME
    return args


def resolve_target_date(args: argparse.Namespace) -> dt.date:
    if args.target_date:
        return dt.date.fromisoformat(args.target_date)
    close_time = parse_time_text(args.close_time or DEFAULT_MARKET_CLOSE_TIME)
    return resolve_latest_closed_trade_date(dt.datetime.now(), close_time)


def resolve_backfill_range(target_date: dt.date, mode: str) -> tuple[dt.date, dt.date]:
    trade_calendar = get_trade_calendar()

    def calendar_dates_between(start_date: dt.date, end_date: dt.date) -> list[dt.date]:
        left = bisect.bisect_left(trade_calendar, start_date)
        right = bisect.bisect_right(trade_calendar, end_date)
        return trade_calendar[left:right]

    print(f"  [resolve_backfill_range] mode={mode} fetching data...", flush=True)
    conn = psycopg2.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute(
                """
                SELECT s.symbol,
                      s.list_date,
                       MIN(d.trade_date) AS earliest_trade_date,
                       MAX(d.trade_date) AS latest_trade_date,
                       COUNT(DISTINCT d.trade_date) AS trade_date_count
                FROM ref.symbol_info s
                LEFT JOIN mkt.daily_bar d ON d.symbol_id = s.id
                WHERE s.asset_class = 'STOCK' AND s.status = 'ACTIVE'
                GROUP BY s.symbol, s.list_date
                ORDER BY s.symbol
                """,
            )
            start_dates: list[dt.date] = []
            for symbol, list_date, earliest_trade_date, latest_trade_date, trade_date_count in cursor.fetchall():
                symbol_text = str(symbol).strip()
                if not is_supported_akshare_stock_symbol(symbol_text):
                    continue

                if latest_trade_date is None:
                    if mode in {"latest", "all"}:
                        start_dates.append(target_date)
                    continue

                if earliest_trade_date is None:
                    if mode in {"latest", "all"}:
                        start_dates.append(target_date)
                    continue

                expected_start_date = clamp_history_start_date(list_date or earliest_trade_date)
                if expected_start_date > target_date:
                    continue

                expected_dates = calendar_dates_between(expected_start_date, target_date)
                if not expected_dates:
                    continue

                latest_covered_dates = calendar_dates_between(expected_start_date, latest_trade_date)
                if latest_trade_date < target_date and int(trade_date_count or 0) == len(latest_covered_dates):
                    if mode in {"latest", "all"}:
                        start_dates.append(expected_dates[len(latest_covered_dates)])
                    continue

                if mode == "latest":
                    continue

                expected_count = len(expected_dates)
                if latest_trade_date >= target_date and int(trade_date_count or 0) >= expected_count:
                    continue

                cursor.execute(
                    """
                    SELECT trade_date
                    FROM mkt.daily_bar
                    WHERE symbol_id = (SELECT id FROM ref.symbol_info WHERE symbol = %s)
                      AND trade_date BETWEEN %s AND %s
                    ORDER BY trade_date
                    """,
                    (symbol_text, expected_start_date, target_date),
                )
                existing_dates = {row[0] for row in cursor.fetchall() if row and row[0]}
                first_missing_trade_date = next(
                    (trade_date for trade_date in expected_dates if trade_date not in existing_dates),
                    None,
                )
                if first_missing_trade_date is not None:
                    start_dates.append(first_missing_trade_date)
    finally:
        conn.close()

    if not start_dates:
        return target_date, target_date
    return min(start_dates), target_date


def build_baostock_update_command(args: argparse.Namespace, start_date: dt.date | None = None) -> list[str]:
    """单线程 Baostock 全量日线更新；start_date 用于填补历史内部缺口"""
    command = [sys.executable, "tools/daily_pipeline.py", "--phase3-only"]
    if args.target_date:
        command.extend(["--date", args.target_date])
    if start_date is not None:
        command.extend(["--start-date", start_date.isoformat()])
    return command

def build_verify_command(args: argparse.Namespace) -> list[str]:
    command = [sys.executable, "tools/verify_daily_update.py", "--sample-limit", str(args.sample_limit)]
    if args.target_date:
        command.extend(["--target-date", args.target_date])
    if args.close_time:
        command.extend(["--close-time", args.close_time])
    return command


def build_derived_backfill_command(start_date: dt.date, end_date: dt.date) -> list[str]:
    return [
        sys.executable,
        "tools/backfill_daily_derived_fields.py",
        "--start-date",
        start_date.isoformat(),
        "--end-date",
        end_date.isoformat(),
        "--limit-sample",
        "5",
    ]


def build_valuation_backfill_command(args: argparse.Namespace, start_date: dt.date, end_date: dt.date) -> list[str]:
    command = [
        sys.executable,
        "tools/backfill_daily_valuation_from_ak.py",
        "--start-date",
        start_date.isoformat(),
        "--end-date",
        end_date.isoformat(),
        "--only-missing",
        "--sleep",
        str(args.valuation_sleep),
    ]
    if args.valuation_limit_symbols > 0:
        command.extend(["--limit-symbols", str(args.valuation_limit_symbols)])
    return command


def build_caps_backfill_command(start_date: dt.date, end_date: dt.date) -> list[str]:
    return [
        sys.executable,
        "tools/backfill_daily_caps_from_existing_shares.py",
        "--start-date",
        start_date.isoformat(),
        "--end-date",
        end_date.isoformat(),
        "--limit-sample",
        "5",
    ]


def build_turnover_backfill_command(start_date: dt.date, end_date: dt.date) -> list[str]:
    return [
        sys.executable,
        "tools/backfill_daily_turnover_rate.py",
        "--start-date",
        start_date.isoformat(),
        "--end-date",
        end_date.isoformat(),
        "--limit-sample",
        "5",
    ]


def build_minute_update_command(target_date: dt.date) -> list[str]:
    return [sys.executable, "tools/update_minute_data.py", "--target-date", target_date.isoformat()]

def build_weekly_monthly_command(target_date: dt.date) -> list[str]:
    return [sys.executable, "tools/update_weekly_monthly.py", "--target-date", target_date.isoformat()]

def build_financial_backfill_command(args: argparse.Namespace) -> list[str]:
    command = [sys.executable, "tools/import_financial_from_jq.py"]
    command.extend(["--limit", str(args.financial_limit)])
    command.extend(["--workers", str(args.financial_workers)])
    return command


def run_step(step_name: str, command: list[str]) -> int:
    print(f"\n=== {step_name} ===")
    print("command: " + " ".join(command))
    completed = subprocess.run(command, cwd=PROJECT_ROOT, check=False)
    print(f"=== {step_name} exit_code={completed.returncode} ===")
    return int(completed.returncode)


def persist_report(report_file: str, payload: dict) -> None:
    if not report_file:
        return

    report_path = Path(report_file)
    if not report_path.is_absolute():
        report_path = PROJECT_ROOT / report_path
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")


def execute_step(step_name: str,
                 command: list[str],
                 *,
                 required: bool,
                 continue_on_failure: bool,
                 results: list[dict]) -> bool:
    exit_code = run_step(step_name, command)
    step_result = {
        "name": step_name,
        "command": command,
        "exit_code": exit_code,
        "required": required,
        "status": "success" if exit_code == 0 else ("failed_required" if required else "failed_optional"),
    }
    results.append(step_result)

    if exit_code == 0:
        return True

    if required:
        print(f"关键步骤失败: {step_name}，停止执行后续步骤")
        return False

    if continue_on_failure:
        print(f"可选步骤失败但继续执行: {step_name}")
        return True

    print(f"可选步骤失败且当前为严格模式，停止执行: {step_name}")
    return False


def main() -> int:
    raw_argv = sys.argv[1:]
    args = parse_args()
    if args.daily_close_profile:
        args = apply_daily_close_profile(args)
    if should_run_interactive(raw_argv):
        args = apply_interactive_profile(args)
        if args is None:
            return 0

    target_date = resolve_target_date(args)

    step_results: list[dict] = []

    # --baostock-only: 仅 Baostock 全量更新, 跳过 resolve_backfill_range
    if args.baostock_only:
        if not execute_step("baostock single-threaded update", build_baostock_update_command(args),
                           required=True, continue_on_failure=False, results=step_results):
            return step_results[-1]["exit_code"]
        return 0

    latest_start_date, latest_end_date = resolve_backfill_range(target_date, "latest")
    history_start_date, history_end_date = resolve_backfill_range(target_date, "history") if args.include_history_gaps else (target_date, target_date)

    update_cmd_builder = build_baostock_update_command

    def finalize(exit_code: int) -> int:
        optional_failures = [item for item in step_results if item["status"] == "failed_optional"]
        required_failures = [item for item in step_results if item["status"] == "failed_required"]
        payload = {
            "target_date": target_date.isoformat(),
            "latest_range": [latest_start_date.isoformat(), latest_end_date.isoformat()],
            "history_enabled": args.include_history_gaps,
            "history_range": [history_start_date.isoformat(), history_end_date.isoformat()],
            "with_financial": args.with_financial,
            "financial_workers": args.financial_workers,
            "continue_on_step_failure": args.continue_on_step_failure,
            "daily_close_profile": args.daily_close_profile,
            "step_results": step_results,
            "optional_failure_count": len(optional_failures),
            "required_failure_count": len(required_failures),
            "status": "failed" if exit_code != 0 else ("partial_success" if optional_failures else "success"),
            "exit_code": exit_code,
        }
        persist_report(args.report_file, payload)
        print(
            "pipeline summary: "
            f"status={payload['status']} required_failure_count={len(required_failures)} optional_failure_count={len(optional_failures)}"
        )
        return exit_code

    print(
        "pipeline plan: "
        f"target_date={target_date} latest_range={latest_start_date}..{latest_end_date} "
        f"history_enabled={args.include_history_gaps} history_range={history_start_date}..{history_end_date} "
        f"with_financial={args.with_financial} financial_workers={args.financial_workers} daily_close_profile={args.daily_close_profile} "
        f"continue_on_step_failure={args.continue_on_step_failure}"
    )

    pipeline_failed = False

    if not execute_step(
        "daily latest supplement",
        update_cmd_builder(args),
        required=True,
        continue_on_failure=args.continue_on_step_failure,
        results=step_results,
    ):
        pipeline_failed = True

    if not pipeline_failed:
        if not args.skip_derived_backfill:
            if not execute_step(
                "derived fields backfill (latest)",
                build_derived_backfill_command(latest_start_date, latest_end_date),
                required=False,
                continue_on_failure=args.continue_on_step_failure,
                results=step_results,
            ):
                if not args.continue_on_step_failure:
                    pipeline_failed = True

        if not pipeline_failed and not args.skip_valuation_backfill:
            if not execute_step(
                "valuation backfill (latest)",
                build_valuation_backfill_command(args, latest_start_date, latest_end_date),
                required=False,
                continue_on_failure=args.continue_on_step_failure,
                results=step_results,
            ):
                if not args.continue_on_step_failure:
                    pipeline_failed = True

        if not pipeline_failed and not args.skip_caps_backfill:
            if not execute_step(
                "market cap fallback backfill (latest)",
                build_caps_backfill_command(latest_start_date, latest_end_date),
                required=False,
                continue_on_failure=args.continue_on_step_failure,
                results=step_results,
            ):
                if not args.continue_on_step_failure:
                    pipeline_failed = True

        if not pipeline_failed and not args.skip_turnover_backfill:
            if not execute_step(
                "turnover rate backfill (latest)",
                build_turnover_backfill_command(latest_start_date, latest_end_date),
                required=False,
                continue_on_failure=args.continue_on_step_failure,
                results=step_results,
            ):
                if not args.continue_on_step_failure:
                    pipeline_failed = True

    if not pipeline_failed and args.with_financial:
        if not execute_step(
            "financial backfill",
            build_financial_backfill_command(args),
            required=False,
            continue_on_failure=args.continue_on_step_failure,
            results=step_results,
        ):
            if not args.continue_on_step_failure:
                pipeline_failed = True

    if not pipeline_failed and args.include_history_gaps and history_start_date <= history_end_date:
        if not execute_step(
            "daily history gap supplement",
            build_baostock_update_command(args, start_date=history_start_date),
            required=False,
            continue_on_failure=args.continue_on_step_failure,
            results=step_results,
        ):
            if not args.continue_on_step_failure:
                pipeline_failed = True

        if not pipeline_failed and not args.skip_derived_backfill:
            if not execute_step(
                "derived fields backfill (history)",
                build_derived_backfill_command(history_start_date, history_end_date),
                required=False,
                continue_on_failure=args.continue_on_step_failure,
                results=step_results,
            ):
                if not args.continue_on_step_failure:
                    pipeline_failed = True

        if not pipeline_failed and not args.skip_valuation_backfill:
            if not execute_step(
                "valuation backfill (history)",
                build_valuation_backfill_command(args, history_start_date, history_end_date),
                required=False,
                continue_on_failure=args.continue_on_step_failure,
                results=step_results,
            ):
                if not args.continue_on_step_failure:
                    pipeline_failed = True

        if not pipeline_failed and not args.skip_caps_backfill:
            if not execute_step(
                "market cap fallback backfill (history)",
                build_caps_backfill_command(history_start_date, history_end_date),
                required=False,
                continue_on_failure=args.continue_on_step_failure,
                results=step_results,
            ):
                if not args.continue_on_step_failure:
                    pipeline_failed = True

        if not pipeline_failed and not args.skip_turnover_backfill:
            if not execute_step(
                "turnover rate backfill (history)",
                build_turnover_backfill_command(history_start_date, history_end_date),
                required=False,
                continue_on_failure=args.continue_on_step_failure,
                results=step_results,
            ):
                if not args.continue_on_step_failure:
                    pipeline_failed = True

    # ↓ 以下步骤不受前面失败影响，始终执行 ↓

    if not execute_step(
        "auto-fix lagging daily (akshare)",
        [sys.executable, "tools/verify_daily_update.py", "--sample-limit", str(args.sample_limit), "--auto-fix"],
        required=False,
        continue_on_failure=True,
        results=step_results,
    ):
        pass

    if not execute_step(
        "minute bars update",
        build_minute_update_command(target_date),
        required=False,
        continue_on_failure=True,
        results=step_results,
    ):
        pass

    if not execute_step(
        "weekly/monthly aggregate",
        build_weekly_monthly_command(target_date),
        required=False,
        continue_on_failure=True,
        results=step_results,
    ):
        pass

    if not execute_step(
        "daily verify",
        build_verify_command(args),
        required=True,
        continue_on_failure=args.continue_on_step_failure,
        results=step_results,
    ):
        print("收口校验失败，请检查落后样本或上游数据延迟")
        return finalize(step_results[-1]["exit_code"])

    print("日线更新、缺失字段回填与收口校验均已完成")
    return finalize(0)


if __name__ == "__main__":
    raise SystemExit(main())