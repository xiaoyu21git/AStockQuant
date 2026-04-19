import argparse
import json
from copy import deepcopy
from datetime import datetime
from pathlib import Path

import pymysql


STAGE_DEFINITIONS = {
    "market": {
        "title": "市场环境",
        "description": "先判断当前市场是否允许承担新增风险。",
        "accentColor": "#2563eb",
    },
    "eligibility": {
        "title": "标的准入",
        "description": "过滤不满足流动性、风控和池子约束的标的。",
        "accentColor": "#0ea5e9",
    },
    "signal": {
        "title": "入场确认",
        "description": "定义候选入场、观察信号和否决条件。",
        "accentColor": "#22c55e",
    },
    "portfolio": {
        "title": "仓位与组合",
        "description": "约束单票、组合和行业暴露。",
        "accentColor": "#14b8a6",
    },
    "rebalance": {
        "title": "调仓退出",
        "description": "定义减仓、止盈、止损和退出动作。",
        "accentColor": "#f59e0b",
    },
    "execution": {
        "title": "执行约束",
        "description": "决定节流、拆单和成交推进约束。",
        "accentColor": "#a855f7",
    },
    "account_risk": {
        "title": "账户风控",
        "description": "账户级回撤、熔断和停机边界。",
        "accentColor": "#ef4444",
    },
}


def build_rule(instance_id, template_id, template_name, summary, phase, file_name, category, term_id, term_name):
    return {
        "instanceId": instance_id,
        "templateId": template_id,
        "templateName": template_name,
        "summary": summary,
        "phase": phase,
        "fileName": file_name,
        "filePath": "",
        "ready": True,
        "termId": term_id,
        "termName": term_name,
        "category": category,
        "defaultInjected": True,
    }


MARKET_GATE_RULES = [
    build_rule(
        "rule_market_gate_trend_neutral",
        "template_risk_market_trend_neutral_allow_entry_v1",
        "趋势中性环境放行模板",
        "市场未进入熊市且广度、趋势强度与回撤压力仍在可承受区间时，放行日线趋势策略继续筛股。",
        "market",
        "risk_market_trend_neutral_allow_entry.yaml",
        "market_gate",
        "market_trend_neutral_allow_entry",
        "趋势中性环境放行",
    ),
    build_rule(
        "rule_market_gate_bull_trend",
        "template_risk_market_bull_trend_allow_entry_v1",
        "牛市趋势放行模板",
        "市场处于牛市趋势阶段且广度、趋势强度同步改善时，放行趋势类新增仓位。",
        "market",
        "risk_market_bull_trend_allow_entry.yaml",
        "market_gate",
        "market_bull_trend_allow_entry",
        "牛市趋势放行新开仓",
    ),
    build_rule(
        "rule_market_gate_sideways_selective",
        "template_risk_market_sideways_selective_entry_v1",
        "震荡市精选放行模板",
        "市场处于震荡阶段且波动受控时，仅放行精选确认类新增仓位。",
        "market",
        "risk_market_sideways_selective_entry.yaml",
        "market_gate",
        "market_sideways_selective_entry",
        "震荡市精选放行",
    ),
]

MARKET_VETO_RULES = [
    build_rule(
        "rule_market_veto_bear_freeze",
        "template_risk_market_bear_freeze_entry_v1",
        "熊市冻结新开仓模板",
        "市场进入熊市或系统性退潮阶段时，优先冻结新增仓位。",
        "market",
        "risk_market_bear_freeze_entry.yaml",
        "market_risk",
        "market_bear_freeze_entry",
        "熊市冻结新开仓",
    )
]

SIGNAL_CORE_RULES = [
    build_rule(
        "rule_signal_core_trend_support_near_ma",
        "template_entry_trend_support_near_ma_v1",
        "趋势支撑邻近候选模板",
        "用于识别贴近 20/60 日线且趋势未明显走坏的趋势延续候选，降低强确认布尔条件导致的长期零命中。",
        "signal",
        "entry_trend_support_near_ma.yaml",
        "entry_pattern",
        "entry_trend_support_near_ma",
        "趋势支撑邻近候选",
    ),
]

REBALANCE_EXIT_RULES = [
    build_rule(
        "rule_rebalance_exit_acceptance_breakdown",
        "template_exit_acceptance_breakdown_v1",
        "承接走弱退出模板",
        "承接明显走弱后执行保护性退出。",
        "rebalance",
        "exit_acceptance_breakdown.yaml",
        "exit_pattern",
        "exit_acceptance_breakdown",
        "承接走弱退出",
    )
]

REBALANCE_SCALE_RULES = [
    build_rule(
        "rule_rebalance_scale_take_profit",
        "template_exit_scale_out_take_profit_v1",
        "分批止盈模板",
        "趋势未完全破坏时先分批兑现利润。",
        "rebalance",
        "exit_scale_out_take_profit.yaml",
        "exit_management",
        "exit_scale_out_take_profit",
        "分批止盈",
    )
]


GROUP_SPECS = {
    "market": [
        {
            "groupId": "market_gate",
            "title": "市场放行组",
            "role": "any_pass",
            "operator": "at_least",
            "description": "趋势中性环境、牛市趋势或震荡市精选任一放行即可进入后续个股筛选。",
            "rules": MARKET_GATE_RULES,
        },
        {
            "groupId": "market_veto",
            "title": "风险否决组",
            "role": "veto",
            "operator": "any",
            "description": "命中熊市或系统性退潮边界时冻结新开仓。",
            "rules": MARKET_VETO_RULES,
        },
    ],
    "eligibility": [
        {
            "groupId": "eligibility_core",
            "title": "基础过滤组",
            "role": "must_pass",
            "operator": "all",
            "description": "仅保留股票池、流动性和交易资格过滤，不放失效模板。",
            "rules": [],
        }
    ],
    "signal": [
        {
            "groupId": "signal_core",
            "title": "核心确认组",
            "role": "must_pass",
            "operator": "any",
            "description": "20 日线回踩与 60 日线回踩任一成立即可进入候选。",
            "rules": SIGNAL_CORE_RULES,
        },
        {
            "groupId": "signal_boost",
            "title": "评分增强组",
            "role": "score_boost",
            "operator": "score_sum",
            "description": "当前先留空，避免把阻断类模板误放到增强组。",
            "rules": [],
        },
        {
            "groupId": "signal_veto",
            "title": "信号否决组",
            "role": "veto",
            "operator": "any",
            "description": "当前先留空，后续再补真正适合日线趋势的否决模板。",
            "rules": [],
        },
    ],
    "portfolio": [
        {
            "groupId": "portfolio_budget",
            "title": "风险预算组",
            "role": "position_management",
            "operator": "all",
            "description": "当前先留空，等待明确的组合暴露与仓位预算模板。",
            "rules": [],
        }
    ],
    "rebalance": [
        {
            "groupId": "rebalance_exit",
            "title": "退出触发组",
            "role": "any_pass",
            "operator": "any",
            "description": "承接走弱命中后执行保护性退出。",
            "rules": REBALANCE_EXIT_RULES,
        },
        {
            "groupId": "rebalance_scale",
            "title": "分批管理组",
            "role": "position_management",
            "operator": "all",
            "description": "盈利后先分批止盈，保留趋势继续观察空间。",
            "rules": REBALANCE_SCALE_RULES,
        },
    ],
    "execution": [
        {
            "groupId": "execution_guard",
            "title": "执行限制组",
            "role": "execution_constraint",
            "operator": "all",
            "description": "当前先留空。",
            "rules": [],
        }
    ],
    "account_risk": [
        {
            "groupId": "account_guard",
            "title": "账户保护组",
            "role": "account_guard",
            "operator": "any",
            "description": "当前先留空，避免账户风控模板与市场否决重复。",
            "rules": [],
        }
    ],
}


def build_stages():
    stages = []
    for stage_id in [
        "market",
        "eligibility",
        "signal",
        "portfolio",
        "rebalance",
        "execution",
        "account_risk",
    ]:
        spec = deepcopy(STAGE_DEFINITIONS[stage_id])
        spec["stageId"] = stage_id
        spec["groups"] = deepcopy(GROUP_SPECS[stage_id])
        stages.append(spec)
    return stages


RULE_TEMPLATE_BINDINGS = [
    {
        "phase": "market",
        "group_id": "market_gate",
        "group_title": "市场放行组",
        "group_role": "any_pass",
        "group_operator": "at_least",
        "template_id": "template_risk_market_trend_neutral_allow_entry_v1",
        "template_display_name": "趋势中性环境放行模板",
        "file_name": "risk_market_trend_neutral_allow_entry.yaml",
        "summary": "市场未进入熊市且广度、趋势强度与回撤压力仍在可承受区间时，放行日线趋势策略继续筛股。",
        "category": "market_gate",
        "term_id": "market_trend_neutral_allow_entry",
        "term_display_name": "趋势中性环境放行",
        "default_injected": True,
    },
    {
        "phase": "market",
        "group_id": "market_gate",
        "group_title": "市场放行组",
        "group_role": "any_pass",
        "group_operator": "at_least",
        "template_id": "template_risk_market_bull_trend_allow_entry_v1",
        "template_display_name": "牛市趋势放行模板",
        "file_name": "risk_market_bull_trend_allow_entry.yaml",
        "summary": "市场处于牛市趋势阶段且广度、趋势强度同步改善时，放行趋势类新增仓位。",
        "category": "market_gate",
        "term_id": "market_bull_trend_allow_entry",
        "term_display_name": "牛市趋势放行新开仓",
        "default_injected": True,
    },
    {
        "phase": "market",
        "group_id": "market_gate",
        "group_title": "市场放行组",
        "group_role": "any_pass",
        "group_operator": "at_least",
        "template_id": "template_risk_market_sideways_selective_entry_v1",
        "template_display_name": "震荡市精选放行模板",
        "file_name": "risk_market_sideways_selective_entry.yaml",
        "summary": "市场处于震荡阶段且波动受控时，仅放行精选确认类新增仓位。",
        "category": "market_gate",
        "term_id": "market_sideways_selective_entry",
        "term_display_name": "震荡市精选放行",
        "default_injected": True,
    },
    {
        "phase": "market",
        "group_id": "market_veto",
        "group_title": "风险否决组",
        "group_role": "veto",
        "group_operator": "any",
        "template_id": "template_risk_market_bear_freeze_entry_v1",
        "template_display_name": "熊市冻结新开仓模板",
        "file_name": "risk_market_bear_freeze_entry.yaml",
        "summary": "市场进入熊市或系统性退潮阶段时，优先冻结新增仓位。",
        "category": "market_risk",
        "term_id": "market_bear_freeze_entry",
        "term_display_name": "熊市冻结新开仓",
        "default_injected": True,
    },
    {
        "phase": "signal",
        "group_id": "signal_core",
        "group_title": "核心确认组",
        "group_role": "must_pass",
        "group_operator": "any",
        "template_id": "template_entry_trend_support_near_ma_v1",
        "template_display_name": "趋势支撑邻近候选模板",
        "file_name": "entry_trend_support_near_ma.yaml",
        "summary": "用于识别贴近 20/60 日线且趋势未明显走坏的趋势延续候选，降低强确认布尔条件导致的长期零命中。",
        "category": "entry_pattern",
        "term_id": "entry_trend_support_near_ma",
        "term_display_name": "趋势支撑邻近候选",
        "default_injected": True,
    },
    {
        "phase": "rebalance",
        "group_id": "rebalance_exit",
        "group_title": "退出触发组",
        "group_role": "any_pass",
        "group_operator": "any",
        "template_id": "template_exit_acceptance_breakdown_v1",
        "template_display_name": "承接走弱退出模板",
        "file_name": "exit_acceptance_breakdown.yaml",
        "summary": "承接明显走弱后执行保护性退出。",
        "category": "exit_pattern",
        "term_id": "exit_acceptance_breakdown",
        "term_display_name": "承接走弱退出",
        "default_injected": True,
    },
    {
        "phase": "rebalance",
        "group_id": "rebalance_scale",
        "group_title": "分批管理组",
        "group_role": "position_management",
        "group_operator": "all",
        "template_id": "template_exit_scale_out_take_profit_v1",
        "template_display_name": "分批止盈模板",
        "file_name": "exit_scale_out_take_profit.yaml",
        "summary": "趋势未完全破坏时先分批兑现利润。",
        "category": "exit_management",
        "term_id": "exit_scale_out_take_profit",
        "term_display_name": "分批止盈",
        "default_injected": True,
    },
]


def build_rule_profile(current_parameters):
    existing_profile = ((current_parameters.get("rule_profile") or {}).get("strategyProfile") or {})
    profile = {
        "strategyType": existing_profile.get("strategyType") or current_parameters.get("selectedStrategyType") or "trend_following",
        "horizon": existing_profile.get("horizon") or "swing",
        "tradingFrequency": existing_profile.get("tradingFrequency") or "low_frequency",
        "marketScope": existing_profile.get("marketScope") or "a_share",
        "executionStyle": existing_profile.get("executionStyle") or "close_confirmed",
    }
    return profile


def update_parameters(parameters):
    next_parameters = deepcopy(parameters)
    strategy_profile = build_rule_profile(parameters)
    composer_state = {
        "version": 1,
        "selectedStageId": "signal",
        "selectedGroupId": "signal_core",
        "stages": build_stages(),
    }
    rule_profile = {
        "version": 1,
        "strategyProfile": strategy_profile,
        "ruleComposerState": composer_state,
    }

    next_parameters["rule_template_bindings"] = deepcopy(RULE_TEMPLATE_BINDINGS)
    next_parameters["rule_template_binding"] = deepcopy(RULE_TEMPLATE_BINDINGS[3])
    next_parameters["rule_composer_state"] = deepcopy(composer_state)
    next_parameters["rule_profile"] = deepcopy(rule_profile)
    next_parameters["selectedStrategyType"] = "trend_following"
    next_parameters["selectedStrategySubtype"] = "trend_following"
    return next_parameters


def clear_latest_backtest(performance_metrics):
    if not isinstance(performance_metrics, dict):
        return performance_metrics
    next_metrics = deepcopy(performance_metrics)
    next_metrics.pop("latestBacktest", None)
    next_metrics.pop("latest_backtest", None)
    return next_metrics


def has_column(cursor, table_name, column_name):
    cursor.execute(f"SHOW COLUMNS FROM {table_name} LIKE %s", (column_name,))
    return cursor.fetchone() is not None


def ensure_backup_dir(base_dir):
    base_dir.mkdir(parents=True, exist_ok=True)
    return base_dir


def main():
    parser = argparse.ArgumentParser(description="Apply minimal trend rule set to a strategy row")
    parser.add_argument("strategy_id")
    parser.add_argument("--db-host", default="127.0.0.1")
    parser.add_argument("--db-port", type=int, default=3306)
    parser.add_argument("--db-name", default="astock_quant")
    parser.add_argument("--db-user", default="root")
    parser.add_argument("--db-password", default="123456a")
    parser.add_argument("--backup-dir", default="tools/backups")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    connection = pymysql.connect(
        host=args.db_host,
        user=args.db_user,
        password=args.db_password,
        database=args.db_name,
        port=args.db_port,
        charset="utf8mb4",
    )

    try:
        with connection.cursor() as cursor:
            performance_metrics_column_exists = has_column(cursor, "strategy", "performance_metrics")

            select_fields = "strategy_id, strategy_name, parameters"
            if performance_metrics_column_exists:
                select_fields += ", performance_metrics"

            cursor.execute(
                f"SELECT {select_fields} FROM strategy WHERE strategy_id=%s",
                (args.strategy_id,),
            )
            row = cursor.fetchone()
            if not row:
                raise RuntimeError(f"strategy row not found: {args.strategy_id}")

            strategy_id, strategy_name, parameters_raw = row[:3]
            performance_metrics_raw = row[3] if performance_metrics_column_exists and len(row) > 3 else None
            parameters = json.loads(parameters_raw) if parameters_raw else {}
            performance_metrics = json.loads(performance_metrics_raw) if performance_metrics_raw else {}

            backup_dir = ensure_backup_dir(Path(args.backup_dir))
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            backup_path = backup_dir / f"{strategy_id}_{timestamp}.json"
            backup_payload = {
                "strategy_id": strategy_id,
                "strategy_name": strategy_name,
                "parameters": parameters,
                "performance_metrics": performance_metrics,
                "performance_metrics_column_exists": performance_metrics_column_exists,
            }
            backup_path.write_text(json.dumps(backup_payload, ensure_ascii=False, indent=2), encoding="utf-8")

            next_parameters = update_parameters(parameters)
            next_performance_metrics = clear_latest_backtest(performance_metrics)
            if "performance_metrics" in next_parameters and isinstance(next_parameters["performance_metrics"], dict):
                next_parameters["performance_metrics"] = clear_latest_backtest(next_parameters["performance_metrics"])

            if not args.dry_run:
                if performance_metrics_column_exists:
                    cursor.execute(
                        "UPDATE strategy SET parameters=%s, performance_metrics=%s WHERE strategy_id=%s",
                        (
                            json.dumps(next_parameters, ensure_ascii=False),
                            json.dumps(next_performance_metrics, ensure_ascii=False),
                            strategy_id,
                        ),
                    )
                else:
                    cursor.execute(
                        "UPDATE strategy SET parameters=%s WHERE strategy_id=%s",
                        (
                            json.dumps(next_parameters, ensure_ascii=False),
                            strategy_id,
                        ),
                    )
                connection.commit()

        print("backup_path=", str(backup_path))
        print("strategy_id=", args.strategy_id)
        print("binding_count=", len(next_parameters.get("rule_template_bindings", [])))
        print("market_gate_count=", len(MARKET_GATE_RULES))
        print("signal_core_count=", len(SIGNAL_CORE_RULES))
        print("rebalance_exit_count=", len(REBALANCE_EXIT_RULES))
        print("rebalance_scale_count=", len(REBALANCE_SCALE_RULES))
        print(
            "cleared_latest_backtest=",
            "latestBacktest" not in next_performance_metrics
            and "latest_backtest" not in next_performance_metrics
            and not (isinstance(next_parameters.get("performance_metrics"), dict)
                     and ("latestBacktest" in next_parameters["performance_metrics"]
                          or "latest_backtest" in next_parameters["performance_metrics"]))
        )
        print("performance_metrics_column_exists=", performance_metrics_column_exists)
        print("dry_run=", args.dry_run)
    finally:
        connection.close()


if __name__ == "__main__":
    main()
