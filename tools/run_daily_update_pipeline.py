"""
run_daily_update_pipeline.py
串联执行日线更新、缺失字段回填与收口校验，适合作为一键入口或计划任务目标。
"""

from __future__ import annotations

import argparse
import datetime as dt
import subprocess
import sys
from pathlib import Path

import pymysql

PROJECT_ROOT = Path(__file__).resolve().parents[1]

if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tools.a_share_symbol_utils import is_mainland_a_share_symbol
from tools.trading_day_utils import DEFAULT_MARKET_CLOSE_TIME, parse_time_text, resolve_latest_closed_trade_date


MYSQL_CONFIG = {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "123456a",
    "database": "astock_quant",
    "charset": "utf8mb4",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="一键执行日线更新、缺失字段回填与收口校验"
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
        "--sample-limit",
        type=int,
        default=20,
        help="校验阶段输出的落后股票样本上限，默认 20",
    )
    parser.add_argument(
        "--skip-derived-backfill",
        action="store_true",
        help="跳过 change_pct/change_amt/amplitude 派生字段回填",
    )
    parser.add_argument(
        "--skip-valuation-backfill",
        action="store_true",
        help="跳过 pe/pb/市值估值回填",
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


def resolve_target_date(args: argparse.Namespace) -> dt.date:
    if args.target_date:
        return dt.date.fromisoformat(args.target_date)
    close_time = parse_time_text(args.close_time or DEFAULT_MARKET_CLOSE_TIME)
    return resolve_latest_closed_trade_date(dt.datetime.now(), close_time)


def resolve_backfill_range(target_date: dt.date) -> tuple[dt.date, dt.date]:
    conn = pymysql.connect(**MYSQL_CONFIG)
    try:
        with conn.cursor() as cursor:
            cursor.execute(
                """
                SELECT s.symbol, MAX(d.trade_date) AS latest_trade_date
                FROM symbol_info s
                LEFT JOIN daily_bar d ON d.symbol = s.symbol
                WHERE s.asset_class = 'STOCK' AND s.status = 'ACTIVE'
                GROUP BY s.symbol
                HAVING latest_trade_date IS NULL OR latest_trade_date < %s
                ORDER BY s.symbol
                """,
                (target_date,),
            )
            start_dates: list[dt.date] = []
            for symbol, latest_trade_date in cursor.fetchall():
                symbol_text = str(symbol).strip()
                if not is_mainland_a_share_symbol(symbol_text):
                    continue
                if latest_trade_date is None:
                    start_dates.append(target_date)
                else:
                    start_dates.append(latest_trade_date + dt.timedelta(days=1))
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


def run_step(step_name: str, command: list[str]) -> int:
    print(f"\n=== {step_name} ===")
    print("command: " + " ".join(command))
    completed = subprocess.run(command, cwd=PROJECT_ROOT, check=False)
    print(f"=== {step_name} exit_code={completed.returncode} ===")
    return int(completed.returncode)


def main() -> int:
    args = parse_args()
    target_date = resolve_target_date(args)
    backfill_start_date, backfill_end_date = resolve_backfill_range(target_date)

    print(
        "pipeline range: "
        f"target_date={target_date} backfill_range={backfill_start_date}..{backfill_end_date}"
    )

    update_exit_code = run_step("daily update", build_update_command(args))
    if update_exit_code != 0:
        print("更新阶段失败，停止执行后续校验")
        return update_exit_code

    if not args.skip_derived_backfill:
        derived_exit_code = run_step(
            "derived fields backfill",
            build_derived_backfill_command(backfill_start_date, backfill_end_date),
        )
        if derived_exit_code != 0:
            print("派生字段回填失败，停止执行后续步骤")
            return derived_exit_code

    if not args.skip_valuation_backfill:
        valuation_exit_code = run_step(
            "valuation backfill",
            build_valuation_backfill_command(args, backfill_start_date, backfill_end_date),
        )
        if valuation_exit_code != 0:
            print("估值回填失败，停止执行后续步骤")
            return valuation_exit_code

    if not args.skip_caps_backfill:
        caps_exit_code = run_step(
            "market cap fallback backfill",
            build_caps_backfill_command(backfill_start_date, backfill_end_date),
        )
        if caps_exit_code != 0:
            print("市值补全失败，停止执行后续步骤")
            return caps_exit_code

    if not args.skip_turnover_backfill:
        turnover_exit_code = run_step(
            "turnover rate backfill",
            build_turnover_backfill_command(backfill_start_date, backfill_end_date),
        )
        if turnover_exit_code != 0:
            print("换手率回填失败，停止执行后续步骤")
            return turnover_exit_code

    verify_exit_code = run_step("daily verify", build_verify_command(args))
    if verify_exit_code != 0:
        print("收口校验失败，请检查落后样本或上游数据延迟")
        return verify_exit_code

    print("日线更新、缺失字段回填与收口校验均已完成")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())