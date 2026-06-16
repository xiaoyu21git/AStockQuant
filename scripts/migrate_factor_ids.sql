-- ═══════════════════════════════════════════════════════════════
-- 迁移 strategy_definitions.factor_ids: 整数 → instance_id 字符串
-- 运行前请先备份: SELECT * FROM strategy_definitions;
-- ═══════════════════════════════════════════════════════════════

-- 1. 查看当前数据
SELECT strategy_id, factor_ids FROM strategy_definitions WHERE factor_ids IS NOT NULL;

-- 2. 查看映射关系
SELECT
    s.strategy_id,
    s.factor_ids AS old_ids,
    GROUP_CONCAT(fi.instance_id) AS new_instance_ids
FROM strategy_definitions s,
JSON_TABLE(s.factor_ids, '$[*]' COLUMNS(fid INT PATH '$')) jt
JOIN factor_instance fi ON fi.factor_id = jt.fid
WHERE s.factor_ids IS NOT NULL AND s.factor_ids != '[]'
GROUP BY s.strategy_id, s.factor_ids;

-- 3. 执行更新 (MySQL 8.0+)
UPDATE strategy_definitions s
SET s.factor_ids = (
    SELECT JSON_ARRAYAGG(fi.instance_id)
    FROM JSON_TABLE(s.factor_ids, '$[*]' COLUMNS(fid INT PATH '$')) jt
    JOIN factor_instance fi ON fi.factor_id = jt.fid
)
WHERE s.factor_ids IS NOT NULL
  AND s.factor_ids != '[]'
  AND s.factor_ids NOT LIKE '%"%';

-- 4. 验证结果
SELECT strategy_id, factor_ids FROM strategy_definitions WHERE factor_ids IS NOT NULL;
