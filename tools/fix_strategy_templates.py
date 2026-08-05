"""补全策略 88575b08 的规则模板绑定 — 增加 TrendFollowing 默认入场/否决/出场模板."""
import psycopg2
import json

STRATEGY_ID = "88575b08-386a-41ff-8cbd-91cabfabf70a"

conn = psycopg2.connect(
    host="127.0.0.1", port=5432, dbname="astock_quant",
    user="astock", password="astock123"
)
cur = conn.cursor()
cur.execute("SELECT parameters FROM live.strategy WHERE strategy_id = %s", (STRATEGY_ID,))
row = cur.fetchone()
if not row or not row[0]:
    print("Strategy not found!")
    conn.close()
    exit(1)

params = row[0] if isinstance(row[0], dict) else json.loads(row[0])
rcs = params.get("rule_composer_state", {})
stages = rcs.get("stages", [])


def find_stage(sid):
    for s in stages:
        if s.get("stageId") == sid:
            return s
    return None


def find_group(stage, gid):
    for g in stage.get("groups", []):
        if g.get("groupId") == gid:
            return g
    return None


GROUP_ROLES = {
    "eligibility_core": "must_pass",
    "signal_core": "must_pass",
    "signal_veto": "veto",
    "signal_boost": "score_boost",
    "rebalance_exit": "any_pass",
    "rebalance_scale": "position_management",
}
GROUP_TITLES = {
    "eligibility_core": "基础过滤组",
    "signal_core": "核心确认组",
    "signal_veto": "信号否决组",
    "signal_boost": "评分增强组",
    "rebalance_exit": "退出触发组",
    "rebalance_scale": "分批管理组",
}
GROUP_OPS = {
    "eligibility_core": "all",
    "signal_core": "any",
    "signal_veto": "any",
    "signal_boost": "score_sum",
    "rebalance_exit": "any",
    "rebalance_scale": "all",
}


def ensure_group(stage, gid):
    g = find_group(stage, gid)
    if g:
        return g
    g = {
        "groupId": gid,
        "groupTitle": GROUP_TITLES.get(gid, ""),
        "groupRole": GROUP_ROLES.get(gid, ""),
        "groupOperator": GROUP_OPS.get(gid, "all"),
        "rules": [],
    }
    stage["groups"].append(g)
    return g


def add_rule(stage, gid, template_id, display_name, summary, category, term_id, term_display):
    g = ensure_group(stage, gid)
    for r in g["rules"]:
        if r.get("templateId") == template_id:
            return False
    g["rules"].append({
        "templateId": template_id,
        "templateDisplayName": display_name,
        "summary": summary,
        "category": category,
        "termId": term_id,
        "termDisplayName": term_display,
        "defaultInjected": True,
    })
    return True


# Ensure stages exist in correct order
stage_order = ["eligibility", "signal", "market", "rebalance"]
stage_titles = {
    "eligibility": "资格过滤",
    "signal": "信号确认",
    "market": "市场闸门",
    "rebalance": "调仓管理",
}
for i, sid in enumerate(stage_order):
    s = find_stage(sid)
    if not s:
        s = {"stageId": sid, "stageTitle": stage_titles.get(sid, sid), "groups": []}
        stages.insert(i, s)

added = 0

# ── ELIGIBILITY ──
if add_rule(
    find_stage("eligibility"), "eligibility_core",
    "template_eligibility_trend_participation_guard_v1",
    "趋势参与资格过滤模板",
    "先过滤流动性不足、趋势失速或偏离均线过大的候选，避免裸信号直接开仓。",
    "eligibility_filter", "trend_participation_guard", "趋势参与资格过滤",
):
    print("+ eligibility: trend_participation_guard")
    added += 1

# ── SIGNAL CORE ──
sig = find_stage("signal")
for tid, dn, sm, term_id, term_dn in [
    ("template_entry_trend_support_near_ma_v1", "趋势支撑邻近候选模板",
     "用于识别贴近20/60日线且趋势未明显走坏的趋势延续候选。",
     "entry_trend_support_near_ma", "趋势支撑邻近候选"),
    ("template_entry_pullback_ma20_support_v1", "回踩20日线支撑模板",
     "回踩MA20获得有效支撑且量价配合时，确认趋势延续入场。",
     "entry_pullback_ma20_support", "回踩MA20支撑"),
    ("template_entry_pullback_ma60_support_v1", "回踩60日线支撑模板",
     "回踩MA60获得有效支撑且量价配合时，确认中期趋势延续入场。",
     "entry_pullback_ma60_support", "回踩MA60支撑"),
    ("template_entry_weak_to_strong_v1", "弱转强入场模板",
     "先弱后强、修复关键价位并重新放量时，输出候选入场。",
     "entry_weak_to_strong", "弱转强入场"),
    ("template_entry_pullback_ma20_support_candidate_v1", "回踩20日线候选模板",
     "趋势运行中回踩20日线并获支撑时，生成候选并加分。",
     "entry_pullback_ma20_support_candidate", "回踩MA20支撑候选"),
    ("template_entry_pullback_ma60_support_candidate_v1", "回踩60日线候选模板",
     "趋势运行中回踩60日线并获支撑时，生成候选并加分。",
     "entry_pullback_ma60_support_candidate", "回踩MA60支撑候选"),
]:
    if add_rule(sig, "signal_core", tid, dn, sm, "entry_pattern", term_id, term_dn):
        print(f"+ signal_core: {tid}")
        added += 1

# ── SIGNAL VETO ──
for tid, dn, sm, term_id, term_dn in [
    ("template_watch_trend_structure_breakdown_v1", "趋势结构破坏阻断模板",
     "价格失守趋势支撑、均线斜率转弱或活跃度塌陷时，阻断继续按趋势候选开仓。",
     "trend_structure_breakdown_watch", "趋势结构破坏阻断"),
    ("template_watch_tail_ramp_next_day_weakening_v1", "尾盘偷袭次日走弱模板",
     "尾盘拉升次日未延续强度，转为弱势震荡时取消追涨。",
     "tail_ramp_next_day_weakening", "尾盘偷袭走弱阻断"),
    ("template_watch_afternoon_chase_then_fade_v1", "午后追涨回落模板",
     "午后追高未守住、晚间攻击结构破坏时阻断追进。",
     "afternoon_chase_then_fade", "午后追涨回落阻断"),
]:
    if add_rule(sig, "signal_veto", tid, dn, sm, "watch_invalidation", term_id, term_dn):
        print(f"+ signal_veto: {tid}")
        added += 1

# ── REBALANCE EXIT (additional) ──
reb = find_stage("rebalance")
for tid, dn, sm, term_id, term_dn in [
    ("template_exit_failed_rebound_engulfing_v1", "反弹失败吞没退出模板",
     "反弹后出现吞没形态，执行保护性退出。",
     "exit_failed_rebound_engulfing", "反弹失败吞没退出"),
    ("template_exit_engulfing_next_day_fade_v1", "吞没次日回落退出模板",
     "吞没形态次日继续低走，确认弱势。",
     "exit_engulfing_next_day_fade", "吞没次日回落退出"),
    ("template_exit_engulfing_first_down_day_confirmed_v1", "吞没首阴确认退出模板",
     "吞没后首根阴线确认且失去承接位时减仓退出。",
     "exit_engulfing_first_down_day", "吞没首阴确认退出"),
    ("template_exit_acceleration_volume_stall_v1", "加速后量能停滞退出模板",
     "加速拉升后量能边际递减、承接弱化时减仓退出。",
     "exit_acceleration_volume_stall", "加速量能停滞退出"),
    ("template_exit_high_volume_stall_reversal_v1", "高位放量滞涨退出模板",
     "放量滞涨后价格停滞转为抛压增强时减仓退出。",
     "exit_high_volume_stall_reversal", "高位放量滞涨退出"),
]:
    if add_rule(reb, "rebalance_exit", tid, dn, sm, "exit_pattern", term_id, term_dn):
        print(f"+ rebalance_exit: {tid}")
        added += 1

if added == 0:
    print("No new templates to add.")
    conn.close()
    exit()

# ── Update DB ──
params["rule_composer_state"] = rcs
params_json = json.dumps(params, ensure_ascii=False)
cur.execute(
    "UPDATE live.strategy SET parameters = %s::jsonb, updated_at = NOW() WHERE strategy_id = %s",
    (params_json, STRATEGY_ID),
)
conn.commit()

total = sum(len(r) for s in stages for g in s.get("groups", []) for r in g.get("rules", []))
print(f"\nDone. Added {added} templates. Total bound: {total}")

# ── Verify ──
cur.execute("SELECT parameters FROM live.strategy WHERE strategy_id = %s", (STRATEGY_ID,))
row = cur.fetchone()
p2 = row[0] if isinstance(row[0], dict) else json.loads(row[0])
rcs2 = p2.get("rule_composer_state", {})
stages2 = rcs2.get("stages", [])
print("\n=== Final rule_composer_state ===")
for s in stages2:
    print(f"stage: {s['stageId']}")
    for g in s.get("groups", []):
        tids = [r["templateId"] for r in g.get("rules", [])]
        print(f"  {g['groupId']} ({len(tids)}): {', '.join(tids)}")

conn.close()
