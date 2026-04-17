"""
run_daily_update_pipeline.py
串联执行日线更新、缺失字段回填与收口校验，适合作为一键入口或计划任务目标。
"""

from __future__ import annotations

import argparse
import bisect
import datetime as dt
import json
import subprocess
import sys
from pathlib import Path

import pymysql

PROJECT_ROOT = Path(__file__).resolve().parents[1]

if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from astock_engine.broker.myquant_broker import DEFAULT_GM_TOKEN
from tools.a_share_symbol_utils import is_supported_akshare_stock_symbol
from tools.trading_day_utils import DEFAULT_MARKET_CLOSE_TIME, get_trade_calendar, parse_time_text, resolve_latest_closed_trade_date


MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
}

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
        help="在日线更新之后顺带补财务数据",
    )
    parser.add_argument(
        "--financial-anchor-dates",
        default="",
        help="传给财务补数脚本的锚点日期，逗号分隔，例如 2025-05-01,2025-09-01",
    )
    parser.add_argument(
        "--financial-limit",
        type=int,
        default=10000,
        help="财务补数单次查询返回上限，默认 10000",
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
        "--skip-gm-valuation-backfill",
        action="store_true",
        help="跳过 GM 估值/市值补全（B 股与 AK 未覆盖部分依赖此步骤）",
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
        "1": "仅对齐最新行情",
        "2": "最新行情 + 历史缺口",
        "3": "最新行情 + 财务数据",
        "4": "最新行情 + 历史缺口 + 财务数据",
    }
    print("\n可执行方案:")
    for key, label in choices.items():
        print(f"  {key}. {label}")

    while True:
        choice = input("请选择方案 [1]: ").strip() or "1"
        if choice in choices:
            print(f"已选择: {choices[choice]}")
            return choice
        print("无效选择，请输入 1-4")


def apply_interactive_profile(args: argparse.Namespace) -> argparse.Namespace | None:
    print("数据更新交互模式")
    print("说明: 默认保留现有派生字段、估值、市值与换手率回填；高级参数仍可通过命令行传入。")

    choice = prompt_menu_choice()
    args.include_history_gaps = choice in {"2", "4"}
    args.with_financial = choice in {"3", "4"}

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

    if args.with_financial:
        args.financial_anchor_dates = prompt_text(
            "财务锚点日期，逗号分隔，回车使用脚本默认",
            args.financial_anchor_dates,
        )

    print("\n即将执行:")
    print(f"  target_date={args.target_date or 'auto'}")
    print(f"  include_history_gaps={args.include_history_gaps}")
    print(f"  with_financial={args.with_financial}")
    if args.with_financial:
        print(f"  financial_anchor_dates={args.financial_anchor_dates or '(default)'}")

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

    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute(
                """
                SELECT s.symbol,
                       MIN(d.trade_date) AS earliest_trade_date,
                       MAX(d.trade_date) AS latest_trade_date,
                       COUNT(DISTINCT d.trade_date) AS trade_date_count
                FROM symbol_info s
                LEFT JOIN daily_bar d ON d.symbol = s.symbol
                WHERE s.asset_class = 'STOCK' AND s.status = 'ACTIVE'
                GROUP BY s.symbol
                ORDER BY s.symbol
                """,
            )
            start_dates: list[dt.date] = []
            for symbol, earliest_trade_date, latest_trade_date, trade_date_count in cursor.fetchall():
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

                expected_dates = calendar_dates_between(earliest_trade_date, target_date)
                if not expected_dates:
                    continue

                latest_covered_dates = calendar_dates_between(earliest_trade_date, latest_trade_date)
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
                    FROM daily_bar
                    WHERE symbol = %s AND trade_date BETWEEN %s AND %s
                    ORDER BY trade_date
                    """,
                    (symbol_text, earliest_trade_date, target_date),
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


def build_update_command(args: argparse.Namespace) -> list[str]:
    command = [sys.executable, "tools/update_daily_data.py"]
    if args.target_date:
        command.extend(["--target-date", args.target_date])
    if args.close_time:
        command.extend(["--close-time", args.close_time])
    if args.wait_until_close:
        command.append("--wait-until-close")
    return command


def build_mode_update_command(args: argparse.Namespace, mode: str) -> list[str]:
    command = build_update_command(args)
    command.extend(["--mode", mode])
    return command


def build_verify_command(args: argparse.Namespace) -> list[str]:
    command = [sys.executable, "tools/verify_daily_update.py", "--sample-limit", str(args.sample_limit)]
    if args.target_date:
        command.extend(["--target-date", args.target_date])
    if args.close_time:
        command.extend(["--close-time", args.close_time])
    return command


def build_financial_command(args: argparse.Namespace) -> list[str]:
    command = [sys.executable, "tools/import_financial_from_jq.py", "--limit", str(args.financial_limit)]
    if args.financial_anchor_dates:
        command.extend(["--anchor-dates", args.financial_anchor_dates])
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


def build_gm_valuation_backfill_command(args: argparse.Namespace, start_date: dt.date, end_date: dt.date) -> list[str]:
    command = [
        sys.executable,
        "tools/backfill_daily_valuation_from_gm.py",
        "--start-date",
        start_date.isoformat(),
        "--end-date",
        end_date.isoformat(),
        "--only-missing",
    ]
    if args.valuation_limit_symbols > 0:
        command.extend(["--limit-symbols", str(args.valuation_limit_symbols)])
    return command


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


def is_gm_token_configured() -> bool:
    import os

    return bool(DEFAULT_GM_TOKEN or os.getenv("GM_TOKEN") or os.getenv("ASTOCK_GM_TOKEN"))


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
    latest_start_date, latest_end_date = resolve_backfill_range(target_date, "latest")
    history_start_date, history_end_date = resolve_backfill_range(target_date, "history") if args.include_history_gaps else (target_date, target_date)

    step_results: list[dict] = []

    def finalize(exit_code: int) -> int:
        optional_failures = [item for item in step_results if item["status"] == "failed_optional"]
        required_failures = [item for item in step_results if item["status"] == "failed_required"]
        payload = {
            "target_date": target_date.isoformat(),
            "latest_range": [latest_start_date.isoformat(), latest_end_date.isoformat()],
            "history_enabled": args.include_history_gaps,
            "history_range": [history_start_date.isoformat(), history_end_date.isoformat()],
            "with_financial": args.with_financial,
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
        f"with_financial={args.with_financial} daily_close_profile={args.daily_close_profile} "
        f"continue_on_step_failure={args.continue_on_step_failure}"
    )

    if not execute_step(
        "daily latest update",
        build_mode_update_command(args, "latest"),
        required=True,
        continue_on_failure=args.continue_on_step_failure,
        results=step_results,
    ):
        return finalize(step_results[-1]["exit_code"])

    if not args.skip_derived_backfill:
        if not execute_step(
            "derived fields backfill (latest)",
            build_derived_backfill_command(latest_start_date, latest_end_date),
            required=False,
            continue_on_failure=args.continue_on_step_failure,
            results=step_results,
        ):
            return finalize(step_results[-1]["exit_code"])

    if not args.skip_valuation_backfill:
        if not execute_step(
            "valuation backfill (latest)",
            build_valuation_backfill_command(args, latest_start_date, latest_end_date),
            required=False,
            continue_on_failure=args.continue_on_step_failure,
            results=step_results,
        ):
            return finalize(step_results[-1]["exit_code"])

    if not args.skip_caps_backfill:
        if not execute_step(
            "market cap fallback backfill (latest)",
            build_caps_backfill_command(latest_start_date, latest_end_date),
            required=False,
            continue_on_failure=args.continue_on_step_failure,
            results=step_results,
        ):
            return finalize(step_results[-1]["exit_code"])

    if not args.skip_gm_valuation_backfill:
        if is_gm_token_configured():
            if not execute_step(
                "gm valuation backfill (latest)",
                build_gm_valuation_backfill_command(args, latest_start_date, latest_end_date),
                required=False,
                continue_on_failure=args.continue_on_step_failure,
                results=step_results,
            ):
                return finalize(step_results[-1]["exit_code"])
        else:
            print("跳过 gm valuation backfill (latest): 未配置 GM token")

    if not args.skip_turnover_backfill:
        if not execute_step(
            "turnover rate backfill (latest)",
            build_turnover_backfill_command(latest_start_date, latest_end_date),
            required=False,
            continue_on_failure=args.continue_on_step_failure,
            results=step_results,
        ):
            return finalize(step_results[-1]["exit_code"])

    if args.include_history_gaps and history_start_date <= history_end_date:
        if not execute_step(
            "daily history gap update",
            build_mode_update_command(args, "history"),
            required=False,
            continue_on_failure=args.continue_on_step_failure,
            results=step_results,
        ):
            return finalize(step_results[-1]["exit_code"])

        if not args.skip_derived_backfill:
            if not execute_step(
                "derived fields backfill (history)",
                build_derived_backfill_command(history_start_date, history_end_date),
                required=False,
                continue_on_failure=args.continue_on_step_failure,
                results=step_results,
            ):
                return finalize(step_results[-1]["exit_code"])

        if not args.skip_valuation_backfill:
            if not execute_step(
                "valuation backfill (history)",
                build_valuation_backfill_command(args, history_start_date, history_end_date),
                required=False,
                continue_on_failure=args.continue_on_step_failure,
                results=step_results,
            ):
                return finalize(step_results[-1]["exit_code"])

        if not args.skip_caps_backfill:
            if not execute_step(
                "market cap fallback backfill (history)",
                build_caps_backfill_command(history_start_date, history_end_date),
                required=False,
                continue_on_failure=args.continue_on_step_failure,
                results=step_results,
            ):
                return finalize(step_results[-1]["exit_code"])

        if not args.skip_gm_valuation_backfill:
            if is_gm_token_configured():
                if not execute_step(
                    "gm valuation backfill (history)",
                    build_gm_valuation_backfill_command(args, history_start_date, history_end_date),
                    required=False,
                    continue_on_failure=args.continue_on_step_failure,
                    results=step_results,
                ):
                    return finalize(step_results[-1]["exit_code"])
            else:
                print("跳过 gm valuation backfill (history): 未配置 GM token")

        if not args.skip_turnover_backfill:
            if not execute_step(
                "turnover rate backfill (history)",
                build_turnover_backfill_command(history_start_date, history_end_date),
                required=False,
                continue_on_failure=args.continue_on_step_failure,
                results=step_results,
            ):
                return finalize(step_results[-1]["exit_code"])

    if args.with_financial:
        if not execute_step(
            "financial import",
            build_financial_command(args),
            required=False,
            continue_on_failure=args.continue_on_step_failure,
            results=step_results,
        ):
            return finalize(step_results[-1]["exit_code"])

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