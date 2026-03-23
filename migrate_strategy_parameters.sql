-- ============================================
-- 迁移脚本：将strategy表的parameters字段从JSON改为TEXT
-- 目的：解决Qt MySQL驱动与JSON字段的兼容性问题
-- 版本：1.0
-- 日期：2026-03-23
-- ============================================

-- 使用目标数据库
USE `astock_quant`;

-- 首先备份数据，以防万一
CREATE TABLE IF NOT EXISTS `strategy_backup_before_migration_20260323` AS
SELECT * FROM `strategy`;

-- 检查当前字段类型
SELECT 
    COLUMN_NAME, 
    DATA_TYPE, 
    COLUMN_TYPE,
    CHARACTER_MAXIMUM_LENGTH,
    IS_NULLABLE,
    COLUMN_DEFAULT,
    COLUMN_COMMENT
FROM INFORMATION_SCHEMA.COLUMNS 
WHERE TABLE_SCHEMA = 'astock_quant' 
AND TABLE_NAME = 'strategy' 
AND COLUMN_NAME = 'parameters';

-- 显示当前数据情况
SELECT 
    COUNT(*) as total_strategies,
    SUM(CASE WHEN parameters IS NULL THEN 1 ELSE 0 END) as null_parameters,
    SUM(CASE WHEN parameters = '{}' THEN 1 ELSE 0 END) as empty_json,
    SUM(CASE WHEN JSON_VALID(parameters) = 1 THEN 1 ELSE 0 END) as valid_json,
    SUM(CASE WHEN JSON_VALID(parameters) = 0 THEN 1 ELSE 0 END) as invalid_json
FROM `strategy`;

-- 执行字段类型修改
-- 注意：将JSON类型改为TEXT，保持相同的字符集和排序规则
ALTER TABLE `strategy` 
MODIFY COLUMN `parameters` TEXT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci 
COMMENT '策略参数模板(JSON格式，TEXT类型兼容Qt驱动)';

-- 验证修改后的字段类型
SELECT 
    COLUMN_NAME, 
    DATA_TYPE, 
    COLUMN_TYPE,
    CHARACTER_MAXIMUM_LENGTH,
    IS_NULLABLE,
    COLUMN_DEFAULT,
    COLUMN_COMMENT
FROM INFORMATION_SCHEMA.COLUMNS 
WHERE TABLE_SCHEMA = 'astock_quant' 
AND TABLE_NAME = 'strategy' 
AND COLUMN_NAME = 'parameters';

-- 验证数据完整性
SELECT 
    COUNT(*) as total_strategies,
    SUM(CASE WHEN parameters IS NULL THEN 1 ELSE 0 END) as null_parameters,
    SUM(CASE WHEN parameters = '{}' THEN 1 ELSE 0 END) as empty_json,
    SUM(CASE WHEN JSON_VALID(parameters) = 1 THEN 1 ELSE 0 END) as valid_json,
    SUM(CASE WHEN JSON_VALID(parameters) = 0 THEN 1 ELSE 0 END) as invalid_json
FROM `strategy`;

-- 验证查询是否正常工作（模拟Qt查询）
SELECT 
    strategy_id, 
    strategy_name, 
    strategy_type,
    LENGTH(parameters) as param_length,
    SUBSTRING(parameters, 1, 50) as param_preview
FROM `strategy` 
ORDER BY created_at DESC 
LIMIT 5;

-- 检查backtest_config表（如果也需要修改）
SELECT 
    COLUMN_NAME, 
    DATA_TYPE, 
    COLUMN_TYPE
FROM INFORMATION_SCHEMA.COLUMNS 
WHERE TABLE_SCHEMA = 'astock_quant' 
AND TABLE_NAME = 'backtest_config' 
AND COLUMN_NAME = 'parameters';

-- 如果需要，同样修改backtest_config表
ALTER TABLE `backtest_config` 
MODIFY COLUMN `parameters` TEXT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci 
COMMENT '策略参数(JSON格式，TEXT类型兼容Qt驱动)';

-- 输出迁移完成信息
SELECT '迁移完成：strategy表parameters字段已从JSON改为TEXT类型' as migration_status;
SELECT NOW() as migration_time;