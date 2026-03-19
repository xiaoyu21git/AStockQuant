-- 创建通用清洗数据集系统
-- 支持所有可复用的数据表：行情、财务、因子、基础信息等
USE `astock_quant`;

-- ============================================
-- 第一步：创建数据源类型表（定义所有可复用的数据类型）
-- ============================================
DROP TABLE IF EXISTS `data_source_type`;
CREATE TABLE IF NOT EXISTS `data_source_type` (
    `type_id` VARCHAR(50) PRIMARY KEY COMMENT '数据类型ID',
    `type_name` VARCHAR(100) NOT NULL COMMENT '数据类型名称',
    `category` ENUM('MARKET', 'FINANCIAL', 'FACTOR', 'BASIC') NOT NULL COMMENT '数据分类',
    `description` TEXT COMMENT '数据描述',
    `main_table` VARCHAR(100) NOT NULL COMMENT '对应的主表名',
    `time_field` VARCHAR(50) DEFAULT NULL COMMENT '时间字段名（如trade_date, report_date）',
    `symbol_field` VARCHAR(50) DEFAULT 'symbol' COMMENT '代码字段名',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='数据源类型表';

-- ============================================
-- 第二步：初始化可复用的数据源类型
-- ============================================
INSERT IGNORE INTO `data_source_type` (`type_id`, `type_name`, `category`, `description`, `main_table`, `time_field`, `symbol_field`) VALUES
-- 行情数据（MARKET）
('DAILY_BAR', '日线行情', 'MARKET', '日K线数据', 'daily_bar', 'trade_date', 'symbol'),
('MINUTE_BAR', '分钟线', 'MARKET', '分钟K线数据', 'minute_bar', 'trade_time', 'symbol'),
('V_DAILY_BAR', '日线视图', 'MARKET', '日线视图（可能包含计算指标）', 'v_daily_bar', 'trade_date', 'symbol'),

-- 财务数据（FINANCIAL）
('FINANCIAL_INDICATOR', '财务指标', 'FINANCIAL', '财务指标数据', 'financial_indicator', 'report_date', 'symbol'),

-- 因子数据（FACTOR）
('FACTORS', '因子定义', 'FACTOR', '因子定义数据', 'factors', NULL, NULL),
('USER_FACTOR', '用户因子', 'FACTOR', '用户自定义因子', 'user_factor', NULL, NULL),
('FACTOR_PERFORMANCE', '因子表现', 'FACTOR', '因子表现数据', 'factor_performance', 'trade_date', 'symbol'),

-- 基础信息（BASIC）
('SYMBOL_INFO', '股票信息', 'BASIC', '股票基础信息', 'symbol_info', NULL, 'symbol');

-- ============================================
-- 第三步：创建清洗数据集表
-- ============================================
DROP TABLE IF EXISTS `cleaned_dataset`;
CREATE TABLE IF NOT EXISTS `cleaned_dataset` (
    `dataset_id` VARCHAR(50) PRIMARY KEY COMMENT '数据集ID',
    `dataset_name` VARCHAR(100) NOT NULL COMMENT '数据集名称',
    `description` TEXT COMMENT '数据集描述',
    `data_type_id` VARCHAR(50) NOT NULL COMMENT '引用的数据类型ID',
    `status` ENUM('ACTIVE', 'ARCHIVED', 'DELETED') DEFAULT 'ACTIVE' COMMENT '状态',
    `created_by` VARCHAR(50) COMMENT '创建者',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    FOREIGN KEY (`data_type_id`) REFERENCES `data_source_type`(`type_id`) ON DELETE RESTRICT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='清洗数据集表';

-- ============================================
-- 第四步：创建数据集内容索引表
-- ============================================
DROP TABLE IF EXISTS `cleaned_dataset_items`;
CREATE TABLE IF NOT EXISTS `cleaned_dataset_items` (
    `dataset_id` VARCHAR(50) NOT NULL COMMENT '数据集ID',
    `symbol` VARCHAR(20) NOT NULL COMMENT '股票代码',
    `time_key` VARCHAR(50) NOT NULL COMMENT '时间键（日期、季度、年份等）',
    `data_status` ENUM('INCLUDED', 'EXCLUDED', 'FLAGGED') DEFAULT 'INCLUDED' COMMENT '数据状态',
    `cleaning_notes` TEXT COMMENT '清洗备注',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`dataset_id`, `symbol`, `time_key`),
    KEY `idx_symbol_time` (`symbol`, `time_key`),
    KEY `idx_dataset_status` (`dataset_id`, `data_status`),
    FOREIGN KEY (`dataset_id`) REFERENCES `cleaned_dataset`(`dataset_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='清洗数据集内容索引表';

-- ============================================
-- 第五步：创建默认数据集
-- ============================================
-- 5.1 默认日线数据集（解决因子回测问题）
INSERT IGNORE INTO `cleaned_dataset` 
(`dataset_id`, `dataset_name`, `description`, `data_type_id`, `status`, `created_by`)
VALUES 
('default_daily', '默认日线数据集', '系统默认的日线清洗数据集，用于因子回测', 'DAILY_BAR', 'ACTIVE', 'system');

-- 5.2 默认财务数据集
INSERT IGNORE INTO `cleaned_dataset` 
(`dataset_id`, `dataset_name`, `description`, `data_type_id`, `status`, `created_by`)
VALUES 
('default_financial', '默认财务数据集', '系统默认的财务数据清洗数据集', 'FINANCIAL_INDICATOR', 'ACTIVE', 'system');

-- 5.3 默认因子数据集
INSERT IGNORE INTO `cleaned_dataset` 
(`dataset_id`, `dataset_name`, `description`, `data_type_id`, `status`, `created_by`)
VALUES 
('default_factor', '默认因子数据集', '系统默认的因子数据清洗数据集', 'FACTOR_PERFORMANCE', 'ACTIVE', 'system');

-- ============================================
-- 第六步：插入测试数据到默认日线数据集
-- ============================================
-- 只插入最近30天的数据，避免数据量过大
INSERT IGNORE INTO `cleaned_dataset_items` 
(`dataset_id`, `symbol`, `time_key`, `data_status`, `cleaning_notes`)
SELECT 
    'default_daily',
    d.symbol,
    d.trade_date,
    'INCLUDED',
    '系统默认清洗数据'
FROM daily_bar d
WHERE d.trade_date >= DATE_SUB(CURDATE(), INTERVAL 30 DAY)
  AND d.symbol IN (
      SELECT DISTINCT symbol 
      FROM daily_bar 
      WHERE trade_date >= DATE_SUB(CURDATE(), INTERVAL 30 DAY)
      ORDER BY symbol 
      LIMIT 100  -- 只取100只股票
  )
LIMIT 5000;  -- 最多5000条记录

-- ============================================
-- 第七步：创建 cleaned_daily_bar 视图（兼容现有代码）
-- ============================================
DROP VIEW IF EXISTS `cleaned_daily_bar`;
CREATE VIEW `cleaned_daily_bar` AS
SELECT 
    d.*,
    i.data_status,
    i.cleaning_notes,
    i.dataset_id
FROM daily_bar d
JOIN cleaned_dataset_items i ON d.symbol = i.symbol AND d.trade_date = i.time_key
WHERE i.dataset_id = 'default_daily'
  AND i.data_status = 'INCLUDED';

-- ============================================
-- 第八步：显示创建结果和统计信息
-- ============================================
SELECT '✅ 通用清洗数据集系统创建完成' AS `message`;
SELECT '    - 支持所有可复用的数据表：行情、财务、因子、基础信息' AS `detail1`;
SELECT '    - 不存储实际数据，只保存索引，避免数据冗余' AS `detail2`;
SELECT '    - 同一组数据可测试不同因子，支持数据集重用' AS `detail3`;

SELECT '📊 系统统计信息：' AS `stats_title`;
SELECT COUNT(*) AS `data_source_types` FROM `data_source_type`;
SELECT COUNT(*) AS `cleaned_datasets` FROM `cleaned_dataset`;
SELECT COUNT(*) AS `cleaned_dataset_items` FROM `cleaned_dataset_items`;
SELECT COUNT(*) AS `cleaned_daily_bar_records` FROM `cleaned_daily_bar`;

SELECT '🔍 数据源类型分布：' AS `source_type_title`;
SELECT category, COUNT(*) as type_count
FROM data_source_type
GROUP BY category
ORDER BY category;

SELECT '📅 默认日线数据集内容（示例）：' AS `dataset_sample_title`;
SELECT symbol, time_key as trade_date, data_status, cleaning_notes
FROM cleaned_dataset_items
WHERE dataset_id = 'default_daily'
ORDER BY time_key DESC, symbol
LIMIT 10;

SELECT '🧪 测试查询（验证因子回测可用性）：' AS `test_title`;
SELECT '-- 查询某日所有股票的收盘价（因子回测常用查询）' AS `example1`;
SELECT 'SELECT symbol, close FROM cleaned_daily_bar WHERE trade_date = "2024-01-15";' AS `sql1`;

SELECT '-- 查询市盈率因子数据' AS `example2`;
SELECT 'SELECT symbol, pe_ratio FROM cleaned_daily_bar WHERE trade_date = "2024-01-15" AND pe_ratio IS NOT NULL;' AS `sql2`;

SELECT '-- 批量查询多日数据' AS `example3`;
SELECT 'SELECT trade_date, symbol, close FROM cleaned_daily_bar WHERE trade_date BETWEEN "2024-01-01" AND "2024-01-31";' AS `sql3`;


