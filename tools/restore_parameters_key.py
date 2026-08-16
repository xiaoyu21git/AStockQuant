import sys
import json

sys.path.insert(0, 'tools')
from db_config import pg_connect

# 因子参数键统一恢复为 "parameters" (2026-08-16 用户裁定):
# 1. 顶层 calculation -> parameters (覆盖旧值; calculation 是当前计算侧实际读取的内容, 平移保持行为不变)
# 2. 嵌套 config.calculation -> config.parameters
# 3. 存储参数内容修补: 恢复后这些值会被计算侧真正读取, 需把会抛异常的旧形状(字符串枚举/空字符串)
#    修成数字枚举, 并把 v2 脚本的 lagEnabled 补成解析器读取的 laggedEnabled
# 用法: python tools/restore_parameters_key.py [--dry-run]

# 枚举数值唯一事实源 = 现行 C++ 枚举 (UI 桥接 FactorMetaService 直接 bind 这些枚举, 引擎解析器按这些数值校验):
#   factor_enums.h: LowVolComponent VOLATILITY=0 DRAWDOWN=1 BETA=2
#                  TechnicalIndicator RSI=0 MACD=1 MA=2 EMA=3 BOLL=4 KDJ=5 ATR=6 OBV=7 VWAP=8 VOLUME_RATIO=9 TURNOVER_STABILITY=10
#   FactorMetricConfig.h: DataFrequency Minute=0 Daily=1 Weekly=2 Monthly=3 Quarterly=4 Yearly=5
#                         StandardizationMethod None=0 ZScore=1 MinMax=2 Rank=3 Percentile=4
# ⚠️ fix_factors.py/fix_factors2.py 注释自称 "低频=2, Z-Score=0, Rank=1, None=2" 与 C++ 枚举任何历史版本都不符,
#   这两脚本写的值从未被引擎读取过(死键), 恢复后必须按因子名嵌入的意图 (zs=ZScore/rank=Rank/none=None/低频=日频) 重新翻译

# 内容修补规则: instance_id 后缀匹配 -> 要写入最终顶层 parameters 的字段
CONTENT_PATCHES = [
    # 46079 低波: 用户确认修补 — 权重 0 会每日报"权重总和不能为0", components "" 会在 loadConfig 抛异常
    ('46079', {'components': [0, 1, 2], 'volatilityWeight': 33.4, 'drawdownWeight': 33.3, 'betaWeight': 33.3}),
    # 技术因子 3 个: technicalIndicators "" (空字符串) 会被解析器抛 "必须是数组"; 按因子名还原选择
    ('63662', {'technicalIndicators': [4]}),        # 布林带 -> BOLL
    ('70261', {'technicalIndicators': [6]}),        # atr14天 -> ATR
    ('14142', {'technicalIndicators': [3]}),        # ema10天 -> EMA
    ('80642', {'technicalIndicators': [6, 3, 4]}),  # atr-ema-布林带
    # Python 脚本 10 行: v1 存字符串("低频"/"Z-Score"/"Rank"/"None", asInt 会抛异常),
    # v2 存的数值沿用错误注释映射(frequency=2 在 C++ 是 Weekly 会把日期锚到上周五; standardization 0/1/2 语义错位)。
    # 按因子名意图重新翻译: 低频->日频(Daily=1), zs->ZScore=1, rank->Rank=3, none->None=0
    ('MOM_20', {'frequency': 1, 'standardization': 1}),  # zscore_标准动量
    ('MOM_60', {'frequency': 1, 'standardization': 3}),  # rank_中期动量
    ('GR_rev', {'frequency': 1, 'standardization': 1}),  # yoy_zs 营收成长
    ('GR_roe', {'frequency': 1, 'standardization': 1}),  # yoy_zs 盈利成长
    ('QL_roe', {'frequency': 1, 'standardization': 1}),  # ttm_zs 高ROE
    ('QL_gross', {'frequency': 1, 'standardization': 1}),# ttm_zs 高毛利
    ('VAL_pe', {'frequency': 1, 'standardization': 1}),  # ttm_zs 低市盈率
    ('VAL_pb', {'frequency': 1, 'standardization': 1}),  # lq_zs 低市净率
    ('SZ_float', {'frequency': 1, 'standardization': 0}),# log_none 小市值
    # LV_60d: components ["volatility"] 字符串数组, 解析器要求数字枚举; 名字 vol_none -> 日频+不标准化
    ('LV_60', {'frequency': 1, 'standardization': 0, 'components': [0]}),
]

# lagEnabled -> laggedEnabled 补齐 (解析器只读 laggedEnabled, 修复文档对非 configurable 因子的既定口径)
LAG_ALIAS_ROWS = ['GR_rev', 'GR_roe', 'QL_gross', 'QL_roe', 'SZ_float', 'VAL_pb', 'VAL_pe', 'LV_60']


def apply_content_patches(fid, params):
    """在最终顶层 parameters 上应用内容修补, 返回 (params, 修补说明列表)"""
    notes = []
    if params is None:
        params = {}
    if not isinstance(params, dict):
        notes.append('parameters 非对象, 重置为空对象')
        params = {}

    for suffix, patch in CONTENT_PATCHES:
        if suffix in fid:
            for key, value in patch.items():
                old = params.get(key)
                params[key] = value
                notes.append('patch %s: %s -> %s' % (key, old, value))

    if any(s in fid for s in LAG_ALIAS_ROWS):
        if params.get('lagEnabled') is True and 'laggedEnabled' not in params:
            params['laggedEnabled'] = True
            notes.append('patch laggedEnabled: 补齐 True (原只有 lagEnabled)')
    return params, notes


def migrate_row(fid, cfg):
    """返回 (新配置, 变更说明列表); 无变化返回 (None, [])"""
    new_cfg = json.loads(json.dumps(cfg, ensure_ascii=False))  # 深拷贝, 比较用
    notes = []

    if 'calculation' in new_cfg:
        notes.append('top calculation -> parameters (原值: %s)' % json.dumps(new_cfg.get('calculation'), ensure_ascii=False)[:120])
        new_cfg['parameters'] = new_cfg.pop('calculation')

    nested = new_cfg.get('config')
    if isinstance(nested, dict) and 'calculation' in nested:
        notes.append('nested config.calculation -> config.parameters (原值: %s)' % json.dumps(nested['calculation'], ensure_ascii=False)[:120])
        nested['parameters'] = nested.pop('calculation')

    params, patch_notes = apply_content_patches(fid, new_cfg.get('parameters'))
    if patch_notes:
        new_cfg['parameters'] = params
        notes.extend(patch_notes)

    if not notes:
        return None, []

    # 判断是否有实质变化 (key 平移且内容相同则视为等价)
    if json.dumps(new_cfg, ensure_ascii=False, sort_keys=True) == json.dumps(cfg, ensure_ascii=False, sort_keys=True):
        return None, []
    return new_cfg, notes


def main():
    dry = '--dry-run' in sys.argv
    conn = pg_connect()
    cur = conn.cursor()
    cur.execute('SELECT instance_id, full_config FROM alpha.factor_instance')
    rows = cur.fetchall()

    changes = []
    for fid, cfg in rows:
        cfg = cfg if isinstance(cfg, dict) else json.loads(cfg)
        new_cfg, notes = migrate_row(fid, cfg)
        if new_cfg is not None:
            changes.append((fid, new_cfg, notes))

    print('%s: %d 行将变更 / 共 %d 行' % ('DRY-RUN' if dry else '写入', len(changes), len(rows)))
    for fid, new_cfg, notes in changes:
        print('== %s' % fid)
        for note in notes:
            print('   - %s' % note)

    if not dry:
        for fid, new_cfg, _ in changes:
            cur.execute('UPDATE alpha.factor_instance SET full_config=%s::jsonb WHERE instance_id=%s',
                        (json.dumps(new_cfg, ensure_ascii=False), fid))
        conn.commit()
        print('已写入并提交')
    conn.close()


if __name__ == '__main__':
    main()
