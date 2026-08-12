-- migrate_strategy_type_enum_name.sql
-- 一次性数据迁移: live.strategy 策略类型从数字键改为 C++ 枚举名字符串
-- 契约: metadata_json.strategyType / parameters.rule_profile.strategyProfile.strategyType
-- 均为大写枚举名 (如 MACHINE_LEARNING_SELECTION), 数字键永久删除
-- 执行: PGPASSWORD=astock123 psql -h 127.0.0.1 -U astock -d astock_quant -f tools/migrate_strategy_type_enum_name.sql
-- 必须在改造后的新二进制首次启动前执行 (新仓储对旧行严格失败)

BEGIN;

-- 1) 机器学习+ai因子20天持仓 (旧 metadata 4/4 错误标记为多因子 → MACHINE_LEARNING_SELECTION 修正)
UPDATE live.strategy SET
  metadata_json = metadata_json - 'strategyTypeIndex' - 'behaviorKind'
                  || '{"strategyType":"MACHINE_LEARNING_SELECTION"}'::jsonb,
  parameters = jsonb_set(parameters, '{rule_profile,strategyProfile}',
      COALESCE(parameters #> '{rule_profile,strategyProfile}', '{}'::jsonb)
      - 'strategyTypeIndex' - 'strategyBehaviorKind'
      || '{"strategyType":"MACHINE_LEARNING_SELECTION"}'::jsonb)
WHERE strategy_id = '862b0b8d-13a1-44c2-aaf0-c150613d77fe';

-- 2) 多因子策略 (旧 metadata 4/4, profile 6/4 矛盾 → MULTI_FACTOR_SELECTION 统一)
UPDATE live.strategy SET
  metadata_json = metadata_json - 'strategyTypeIndex' - 'behaviorKind'
                  || '{"strategyType":"MULTI_FACTOR_SELECTION"}'::jsonb,
  parameters = jsonb_set(parameters, '{rule_profile,strategyProfile}',
      COALESCE(parameters #> '{rule_profile,strategyProfile}', '{}'::jsonb)
      - 'strategyTypeIndex' - 'strategyBehaviorKind'
      || '{"strategyType":"MULTI_FACTOR_SELECTION"}'::jsonb)
WHERE strategy_id = '162bff22-041f-47b4-a415-ab5a18aecabe';

-- 3) 10.24 基线 (旧 metadata 0/4, bk=4 系 JS 强制 MultiFactor 污染
--    用户裁决: 自有双均线信号+因子叠加是正常行为非多因子 → DOUBLE_MOVING_AVERAGE)
UPDATE live.strategy SET
  metadata_json = metadata_json - 'strategyTypeIndex' - 'behaviorKind'
                  || '{"strategyType":"DOUBLE_MOVING_AVERAGE"}'::jsonb,
  parameters = jsonb_set(parameters, '{rule_profile,strategyProfile}',
      COALESCE(parameters #> '{rule_profile,strategyProfile}', '{}'::jsonb)
      - 'strategyTypeIndex' - 'strategyBehaviorKind'
      || '{"strategyType":"DOUBLE_MOVING_AVERAGE"}'::jsonb)
WHERE strategy_id = '88575b08-386a-41ff-8cbd-91cabfabf70a';

-- 4) 回填历史回测记录 behavior_kind (ML=5, 多因子=4, 趋势=0)
--    88575b08 全部行已为 0, 回填为无操作; 862b0b8d/162bff22 旧行为 0 是错的, 一并修正
UPDATE live.strategy_backtest_results b SET behavior_kind = m.kind
FROM (VALUES ('862b0b8d-13a1-44c2-aaf0-c150613d77fe', 5),
             ('162bff22-041f-47b4-a415-ab5a18aecabe', 4),
             ('88575b08-386a-41ff-8cbd-91cabfabf70a', 0)) AS m(strategy_id, kind)
WHERE b.strategy_id = m.strategy_id;

COMMIT;

-- ══ 校验 (全部应通过) ══

-- 5) 三行 strategyType 均为预期枚举名, 且 metadata 与 profile 同值
SELECT strategy_id,
       metadata_json->>'strategyType'                     AS metadata_type,
       parameters#>'{rule_profile,strategyProfile}'->>'strategyType' AS profile_type,
       (metadata_json->>'strategyType') IS DISTINCT FROM
           (parameters#>'{rule_profile,strategyProfile}'->>'strategyType') AS mismatch
FROM live.strategy
ORDER BY strategy_id;

-- 6) 数字键残留检查: 应返回 0 行
SELECT strategy_id, 'metadata.strategyTypeIndex' AS residue
FROM live.strategy WHERE metadata_json ? 'strategyTypeIndex'
UNION ALL
SELECT strategy_id, 'metadata.behaviorKind'
FROM live.strategy WHERE metadata_json ? 'behaviorKind'
UNION ALL
SELECT strategy_id, 'profile.strategyTypeIndex'
FROM live.strategy WHERE (parameters#>'{rule_profile,strategyProfile}') ? 'strategyTypeIndex'
UNION ALL
SELECT strategy_id, 'profile.strategyBehaviorKind'
FROM live.strategy WHERE (parameters#>'{rule_profile,strategyProfile}') ? 'strategyBehaviorKind';

-- 7) 回测记录 behavior_kind 回填结果
SELECT strategy_id, count(*) AS rows, min(behavior_kind) AS min_bk, max(behavior_kind) AS max_bk
FROM live.strategy_backtest_results
WHERE strategy_id IN ('862b0b8d-13a1-44c2-aaf0-c150613d77fe',
                      '162bff22-041f-47b4-a415-ab5a18aecabe',
                      '88575b08-386a-41ff-8cbd-91cabfabf70a')
GROUP BY strategy_id;
