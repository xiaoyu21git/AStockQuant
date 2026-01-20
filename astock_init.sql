-- ============================================
-- ASTOCK Quant Engine Database Initialization
-- 版本: 1.0
-- 描述: 量化交易系统核心数据库结构
-- 数据库: astock_quant
-- 字符集: utf8mb4
-- 引擎: InnoDB
-- ============================================

-- 切换到目标数据库（请先确保astock_quant数据库存在）
USE `astock_quant`;

-- 设置SQL模式和执行环境
SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;
SET TIME_ZONE = '+08:00'; -- 设置为东八区（北京时间）

-- ============================================
-- 1. 基础信息表（元数据）
-- ============================================

-- 股票/标的物基本信息表
CREATE TABLE IF NOT EXISTS `symbol_info` (
    `symbol_id` INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '标的ID',
    `symbol` VARCHAR(20) NOT NULL COMMENT '标的代码，如: 000001.SZ',
    `name` VARCHAR(100) NOT NULL COMMENT '标的名称',
    `exchange` ENUM('SH', 'SZ', 'BJ', 'HK', 'US') NOT NULL COMMENT '交易所',
    `asset_class` ENUM('STOCK', 'ETF', 'INDEX', 'FUTURE', 'BOND', 'CRYPTO') DEFAULT 'STOCK' COMMENT '资产类别',
    `industry` VARCHAR(50) DEFAULT NULL COMMENT '所属行业',
    `list_date` DATE DEFAULT NULL COMMENT '上市日期',
    `delist_date` DATE DEFAULT NULL COMMENT '退市日期',
    `status` ENUM('ACTIVE', 'DELISTED', 'SUSPENDED', 'ST', '*ST') DEFAULT 'ACTIVE' COMMENT '状态',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`symbol_id`),
    UNIQUE KEY `uk_symbol` (`symbol`),
    KEY `idx_exchange` (`exchange`),
    KEY `idx_industry` (`industry`),
    KEY `idx_status` (`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='标的物基本信息表';

-- ============================================
-- 2. 市场数据表（核心数据）
-- ============================================

-- 日线行情数据表（按年份分区，适合大量数据）
CREATE TABLE IF NOT EXISTS `daily_bar` (
    `bar_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '行情ID',
    `symbol_id` INT UNSIGNED NOT NULL COMMENT '标的ID',
    `trade_date` DATE NOT NULL COMMENT '交易日期',
    `open` DECIMAL(12, 4) NOT NULL COMMENT '开盘价',
    `high` DECIMAL(12, 4) NOT NULL COMMENT '最高价',
    `low` DECIMAL(12, 4) NOT NULL COMMENT '最低价',
    `close` DECIMAL(12, 4) NOT NULL COMMENT '收盘价',
    `pre_close` DECIMAL(12, 4) DEFAULT NULL COMMENT '前收盘价',
    `volume` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '成交量(股/手)',
    `turnover` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '成交额(元)',
    `change_pct` DECIMAL(8, 4) DEFAULT NULL COMMENT '涨跌幅(%)',
    `change_amt` DECIMAL(12, 4) DEFAULT NULL COMMENT '涨跌额',
    `amplitude` DECIMAL(8, 4) DEFAULT NULL COMMENT '振幅(%)',
    `turnover_rate` DECIMAL(8, 4) DEFAULT NULL COMMENT '换手率(%)',
    `pe` DECIMAL(10, 4) DEFAULT NULL COMMENT '市盈率',
    `pb` DECIMAL(10, 4) DEFAULT NULL COMMENT '市净率',
    `market_cap` DECIMAL(20, 4) DEFAULT NULL COMMENT '总市值',
    `circulating_market_cap` DECIMAL(20, 4) DEFAULT NULL COMMENT '流通市值',
    `data_source` VARCHAR(50) DEFAULT 'UNKNOWN' COMMENT '数据源',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`bar_id`, `trade_date`),
    UNIQUE KEY `uk_symbol_date` (`symbol_id`, `trade_date`),
    KEY `idx_symbol_id` (`symbol_id`),
    KEY `idx_trade_date` (`trade_date`),
    CONSTRAINT `fk_daily_bar_symbol` FOREIGN KEY (`symbol_id`) REFERENCES `symbol_info` (`symbol_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='日线行情数据表'
-- 启用表分区（按年分区，大幅提升历史数据查询性能）
PARTITION BY RANGE (YEAR(`trade_date`)) (
    PARTITION p2020 VALUES LESS THAN (2021),
    PARTITION p2021 VALUES LESS THAN (2022),
    PARTITION p2022 VALUES LESS THAN (2023),
    PARTITION p2023 VALUES LESS THAN (2024),
    PARTITION p2024 VALUES LESS THAN (2025),
    PARTITION p2025 VALUES LESS THAN (2026),
    PARTITION p_future VALUES LESS THAN MAXVALUE
);

-- 分钟线行情数据表（存储多周期K线）
CREATE TABLE IF NOT EXISTS `minute_bar` (
    `bar_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '行情ID',
    `symbol_id` INT UNSIGNED NOT NULL COMMENT '标的ID',
    `timeframe` ENUM('1min', '5min', '15min', '30min', '60min') NOT NULL DEFAULT '1min' COMMENT '时间周期',
    `bar_time` DATETIME NOT NULL COMMENT 'K线时间(周期开始时间)',
    `open` DECIMAL(12, 4) NOT NULL COMMENT '开盘价',
    `high` DECIMAL(12, 4) NOT NULL COMMENT '最高价',
    `low` DECIMAL(12, 4) NOT NULL COMMENT '最低价',
    `close` DECIMAL(12, 4) NOT NULL COMMENT '收盘价',
    `volume` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '成交量',
    `turnover` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '成交额',
    `vwap` DECIMAL(12, 4) DEFAULT NULL COMMENT '成交量加权平均价',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`bar_id`),
    UNIQUE KEY `uk_symbol_timeframe_time` (`symbol_id`, `timeframe`, `bar_time`),
    KEY `idx_symbol_timeframe` (`symbol_id`, `timeframe`),
    KEY `idx_bar_time` (`bar_time`),
    KEY `idx_symbol_bar_time` (`symbol_id`, `bar_time`),
    CONSTRAINT `fk_minute_bar_symbol` FOREIGN KEY (`symbol_id`) REFERENCES `symbol_info` (`symbol_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='分钟线行情数据表';

-- ============================================
-- 3. 财务数据表
-- ============================================

-- 财务指标表（季度/年度）
CREATE TABLE IF NOT EXISTS `financial_indicator` (
    `indicator_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '指标ID',
    `symbol_id` INT UNSIGNED NOT NULL COMMENT '标的ID',
    `report_date` DATE NOT NULL COMMENT '报告期',
    `report_type` ENUM('Q1', 'Q2', 'Q3', 'Q4', 'FY') NOT NULL COMMENT '报告类型',
    `eps` DECIMAL(10, 4) DEFAULT NULL COMMENT '每股收益',
    `bps` DECIMAL(10, 4) DEFAULT NULL COMMENT '每股净资产',
    `roa` DECIMAL(8, 4) DEFAULT NULL COMMENT '总资产收益率',
    `roe` DECIMAL(8, 4) DEFAULT NULL COMMENT '净资产收益率',
    `profit_margin` DECIMAL(8, 4) DEFAULT NULL COMMENT '净利率',
    `debt_to_equity` DECIMAL(8, 4) DEFAULT NULL COMMENT '资产负债率',
    `current_ratio` DECIMAL(8, 4) DEFAULT NULL COMMENT '流动比率',
    `quick_ratio` DECIMAL(8, 4) DEFAULT NULL COMMENT '速动比率',
    `operating_cash_flow` DECIMAL(20, 4) DEFAULT NULL COMMENT '经营活动现金流',
    `investing_cash_flow` DECIMAL(20, 4) DEFAULT NULL COMMENT '投资活动现金流',
    `financing_cash_flow` DECIMAL(20, 4) DEFAULT NULL COMMENT '筹资活动现金流',
    `total_revenue` DECIMAL(20, 4) DEFAULT NULL COMMENT '营业收入',
    `net_profit` DECIMAL(20, 4) DEFAULT NULL COMMENT '净利润',
    `total_assets` DECIMAL(20, 4) DEFAULT NULL COMMENT '总资产',
    `total_liabilities` DECIMAL(20, 4) DEFAULT NULL COMMENT '总负债',
    `equity` DECIMAL(20, 4) DEFAULT NULL COMMENT '所有者权益',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`indicator_id`),
    UNIQUE KEY `uk_symbol_report` (`symbol_id`, `report_date`, `report_type`),
    KEY `idx_symbol_id` (`symbol_id`),
    KEY `idx_report_date` (`report_date`),
    CONSTRAINT `fk_financial_symbol` FOREIGN KEY (`symbol_id`) REFERENCES `symbol_info` (`symbol_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='财务指标表';

-- ============================================
-- 4. 策略与回测系统表
-- ============================================

-- 策略定义表
CREATE TABLE IF NOT EXISTS `strategy` (
    `strategy_id` INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '策略ID',
    `strategy_code` VARCHAR(100) NOT NULL COMMENT '策略代码(唯一标识)',
    `strategy_name` VARCHAR(200) NOT NULL COMMENT '策略名称',
    `strategy_type` ENUM('ALPHA', 'ARBITRAGE', 'TREND', 'MEAN_REVERSION', 'HFT', 'PORTFOLIO') DEFAULT 'ALPHA' COMMENT '策略类型',
    `description` TEXT COMMENT '策略描述',
    `version` VARCHAR(20) DEFAULT '1.0.0' COMMENT '策略版本',
    `author` VARCHAR(100) DEFAULT NULL COMMENT '作者',
    `language` ENUM('PYTHON', 'CPP', 'JULIA', 'R') DEFAULT 'PYTHON' COMMENT '实现语言',
    `status` ENUM('ACTIVE', 'INACTIVE', 'TESTING', 'ARCHIVED') DEFAULT 'ACTIVE' COMMENT '状态',
    `parameters` JSON COMMENT '策略参数模板(JSON格式)',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`strategy_id`),
    UNIQUE KEY `uk_strategy_code` (`strategy_code`),
    KEY `idx_strategy_type` (`strategy_type`),
    KEY `idx_status` (`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='策略定义表';

-- 回测配置表
CREATE TABLE IF NOT EXISTS `backtest_config` (
    `config_id` INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '配置ID',
    `config_name` VARCHAR(200) NOT NULL COMMENT '配置名称',
    `strategy_id` INT UNSIGNED NOT NULL COMMENT '策略ID',
    `start_date` DATE NOT NULL COMMENT '回测开始日期',
    `end_date` DATE NOT NULL COMMENT '回测结束日期',
    `initial_capital` DECIMAL(15, 4) NOT NULL DEFAULT 1000000.0 COMMENT '初始资金',
    `benchmark` VARCHAR(20) DEFAULT '000300.SH' COMMENT '基准指数',
    `commission_rate` DECIMAL(8, 6) DEFAULT 0.0003 COMMENT '佣金费率',
    `slippage_rate` DECIMAL(8, 6) DEFAULT 0.0001 COMMENT '滑点费率',
    `parameters` JSON NOT NULL COMMENT '策略参数(JSON格式)',
    `description` TEXT COMMENT '配置描述',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`config_id`),
    KEY `fk_backtest_strategy` (`strategy_id`),
    KEY `idx_time_range` (`start_date`, `end_date`),
    CONSTRAINT `fk_backtest_strategy` FOREIGN KEY (`strategy_id`) REFERENCES `strategy` (`strategy_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='回测配置表';

-- 回测结果汇总表
CREATE TABLE IF NOT EXISTS `backtest_summary` (
    `summary_id` INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '结果ID',
    `config_id` INT UNSIGNED NOT NULL COMMENT '配置ID',
    `backtest_id` VARCHAR(100) NOT NULL COMMENT '回测任务ID(唯一)',
    `total_return` DECIMAL(10, 4) NOT NULL COMMENT '总收益率(%)',
    `annual_return` DECIMAL(10, 4) DEFAULT NULL COMMENT '年化收益率(%)',
    `sharpe_ratio` DECIMAL(10, 4) DEFAULT NULL COMMENT '夏普比率',
    `max_drawdown` DECIMAL(10, 4) NOT NULL COMMENT '最大回撤(%)',
    `volatility` DECIMAL(10, 4) DEFAULT NULL COMMENT '波动率',
    `win_rate` DECIMAL(8, 4) DEFAULT NULL COMMENT '胜率(%)',
    `profit_loss_ratio` DECIMAL(8, 4) DEFAULT NULL COMMENT '盈亏比',
    `total_trades` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '总交易次数',
    `avg_holding_period` INT UNSIGNED DEFAULT NULL COMMENT '平均持仓周期(天)',
    `alpha` DECIMAL(10, 4) DEFAULT NULL COMMENT 'Alpha',
    `beta` DECIMAL(10, 4) DEFAULT NULL COMMENT 'Beta',
    `information_ratio` DECIMAL(10, 4) DEFAULT NULL COMMENT '信息比率',
    `benchmark_return` DECIMAL(10, 4) DEFAULT NULL COMMENT '基准收益率(%)',
    `final_equity` DECIMAL(15, 4) NOT NULL COMMENT '最终权益',
    `status` ENUM('RUNNING', 'COMPLETED', 'FAILED', 'CANCELLED') DEFAULT 'COMPLETED' COMMENT '状态',
    `start_time` DATETIME NOT NULL COMMENT '回测开始时间',
    `end_time` DATETIME DEFAULT NULL COMMENT '回测结束时间',
    `duration_seconds` INT UNSIGNED DEFAULT NULL COMMENT '回测耗时(秒)',
    `log_path` VARCHAR(500) DEFAULT NULL COMMENT '日志文件路径',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`summary_id`),
    UNIQUE KEY `uk_backtest_id` (`backtest_id`),
    KEY `fk_summary_config` (`config_id`),
    KEY `idx_status` (`status`),
    KEY `idx_created_at` (`created_at`),
    CONSTRAINT `fk_summary_config` FOREIGN KEY (`config_id`) REFERENCES `backtest_config` (`config_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='回测结果汇总表';

-- 交易记录明细表
CREATE TABLE IF NOT EXISTS `trade_record` (
    `trade_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '交易ID',
    `summary_id` INT UNSIGNED NOT NULL COMMENT '回测结果ID',
    `symbol_id` INT UNSIGNED NOT NULL COMMENT '标的ID',
    `trade_time` DATETIME NOT NULL COMMENT '交易时间',
    `trade_type` ENUM('BUY', 'SELL', 'SHORT', 'COVER') NOT NULL COMMENT '交易类型',
    `quantity` INT NOT NULL COMMENT '成交数量',
    `price` DECIMAL(12, 4) NOT NULL COMMENT '成交价格',
    `amount` DECIMAL(15, 4) NOT NULL COMMENT '成交金额',
    `commission` DECIMAL(10, 4) DEFAULT 0.0 COMMENT '手续费',
    `slippage` DECIMAL(10, 4) DEFAULT 0.0 COMMENT '滑点成本',
    `net_amount` DECIMAL(15, 4) NOT NULL COMMENT '净成交金额',
    `position_before` INT DEFAULT NULL COMMENT '交易前持仓',
    `position_after` INT DEFAULT NULL COMMENT '交易后持仓',
    `profit` DECIMAL(12, 4) DEFAULT NULL COMMENT '该笔交易盈亏(平仓时计算)',
    `trade_reason` VARCHAR(500) DEFAULT NULL COMMENT '交易原因/信号',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`trade_id`),
    KEY `fk_trade_summary` (`summary_id`),
    KEY `fk_trade_symbol` (`symbol_id`),
    KEY `idx_trade_time` (`trade_time`),
    KEY `idx_symbol_time` (`symbol_id`, `trade_time`),
    CONSTRAINT `fk_trade_summary` FOREIGN KEY (`summary_id`) REFERENCES `backtest_summary` (`summary_id`) ON DELETE CASCADE,
    CONSTRAINT `fk_trade_symbol` FOREIGN KEY (`symbol_id`) REFERENCES `symbol_info` (`symbol_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='交易记录明细表';

-- 每日持仓与权益表
CREATE TABLE IF NOT EXISTS `daily_position` (
    `position_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '持仓ID',
    `summary_id` INT UNSIGNED NOT NULL COMMENT '回测结果ID',
    `trade_date` DATE NOT NULL COMMENT '交易日',
    `symbol_id` INT UNSIGNED NOT NULL COMMENT '标的ID',
    `position` INT NOT NULL DEFAULT 0 COMMENT '持仓数量',
    `avg_cost` DECIMAL(12, 4) NOT NULL COMMENT '平均成本',
    `market_value` DECIMAL(15, 4) NOT NULL COMMENT '市值',
    `floating_pnl` DECIMAL(15, 4) DEFAULT 0.0 COMMENT '浮动盈亏',
    `realized_pnl` DECIMAL(15, 4) DEFAULT 0.0 COMMENT '已实现盈亏',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`position_id`),
    UNIQUE KEY `uk_summary_date_symbol` (`summary_id`, `trade_date`, `symbol_id`),
    KEY `fk_position_summary` (`summary_id`),
    KEY `fk_position_symbol` (`symbol_id`),
    KEY `idx_trade_date` (`trade_date`),
    CONSTRAINT `fk_position_summary` FOREIGN KEY (`summary_id`) REFERENCES `backtest_summary` (`summary_id`) ON DELETE CASCADE,
    CONSTRAINT `fk_position_symbol` FOREIGN KEY (`symbol_id`) REFERENCES `symbol_info` (`symbol_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='每日持仓与权益表';

-- ============================================
-- 5. 实时监控与系统管理表
-- ============================================

-- 系统事件日志表
CREATE TABLE IF NOT EXISTS `system_event` (
    `event_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '事件ID',
    `event_type` VARCHAR(50) NOT NULL COMMENT '事件类型',
    `event_level` ENUM('DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL') DEFAULT 'INFO' COMMENT '事件级别',
    `module` VARCHAR(100) DEFAULT NULL COMMENT '模块名称',
    `message` TEXT NOT NULL COMMENT '事件内容',
    `details` JSON DEFAULT NULL COMMENT '详细信息(JSON格式)',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`event_id`),
    KEY `idx_event_type` (`event_type`),
    KEY `idx_event_level` (`event_level`),
    KEY `idx_created_at` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='系统事件日志表';

-- 数据更新日志表
CREATE TABLE IF NOT EXISTS `data_update_log` (
    `log_id` INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '日志ID',
    `data_type` ENUM('DAILY_BAR', 'MINUTE_BAR', 'FINANCIAL', 'SYMBOL_INFO') NOT NULL COMMENT '数据类型',
    `update_date` DATE NOT NULL COMMENT '更新日期',
    `start_time` DATETIME NOT NULL COMMENT '开始时间',
    `end_time` DATETIME DEFAULT NULL COMMENT '结束时间',
    `total_records` INT UNSIGNED DEFAULT 0 COMMENT '总记录数',
    `success_records` INT UNSIGNED DEFAULT 0 COMMENT '成功记录数',
    `failed_records` INT UNSIGNED DEFAULT 0 COMMENT '失败记录数',
    `status` ENUM('RUNNING', 'SUCCESS', 'FAILED', 'PARTIAL') DEFAULT 'SUCCESS' COMMENT '状态',
    `error_message` TEXT DEFAULT NULL COMMENT '错误信息',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`log_id`),
    UNIQUE KEY `uk_type_date` (`data_type`, `update_date`),
    KEY `idx_update_date` (`update_date`),
    KEY `idx_status` (`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='数据更新日志表';

-- ============================================
-- 6. 视图（简化常用查询）
-- ============================================

-- 日线行情视图（关联股票信息）
CREATE OR REPLACE VIEW `v_daily_bar` AS
SELECT 
    db.`bar_id`,
    si.`symbol`,
    si.`name`,
    si.`exchange`,
    db.`trade_date`,
    db.`open`,
    db.`high`,
    db.`low`,
    db.`close`,
    db.`volume`,
    db.`turnover`,
    db.`change_pct`,
    db.`turnover_rate`
FROM `daily_bar` db
JOIN `symbol_info` si ON db.`symbol_id` = si.`symbol_id`
WHERE si.`status` = 'ACTIVE';

-- 策略回测概览视图
CREATE OR REPLACE VIEW `v_backtest_overview` AS
SELECT 
    bs.`backtest_id`,
    s.`strategy_name`,
    bc.`config_name`,
    bc.`start_date`,
    bc.`end_date`,
    bc.`initial_capital`,
    bs.`total_return`,
    bs.`annual_return`,
    bs.`sharpe_ratio`,
    bs.`max_drawdown`,
    bs.`win_rate`,
    bs.`total_trades`,
    bs.`start_time`,
    bs.`end_time`,
    bs.`duration_seconds`
FROM `backtest_summary` bs
JOIN `backtest_config` bc ON bs.`config_id` = bc.`config_id`
JOIN `strategy` s ON bc.`strategy_id` = s.`strategy_id`;

-- ============================================
-- 7. 存储过程与函数（可选，高级功能）
-- ============================================

-- 计算回测统计指标的存储过程
DELIMITER //
CREATE PROCEDURE `sp_calculate_backtest_metrics`(
    IN p_backtest_id VARCHAR(100)
)
BEGIN
    -- 这里可以编写复杂的统计计算逻辑
    -- 例如计算每日收益序列、滚动夏普比率等
    SELECT 'Metrics calculation placeholder' AS result;
END //
DELIMITER ;

-- ============================================
-- 8. 插入示例数据（用于测试）
-- ============================================

-- 插入示例股票信息
INSERT IGNORE INTO `symbol_info` (`symbol`, `name`, `exchange`, `industry`, `list_date`) VALUES
('000001.SZ', '平安银行', 'SZ', '银行', '1991-04-03'),
('000002.SZ', '万科A', 'SZ', '房地产', '1991-01-29'),
('000300.SH', '沪深300', 'SH', '指数', '2005-04-08'),
('000905.SH', '中证500', 'SH', '指数', '2007-01-15'),
('600000.SH', '浦发银行', 'SH', '银行', '1999-11-10'),
('600036.SH', '招商银行', 'SH', '银行', '2002-04-09'),
('AAPL', '苹果公司', 'US', '科技', '1980-12-12'),
('MSFT', '微软公司', 'US', '科技', '1986-03-13');

-- 插入示例策略
INSERT IGNORE INTO `strategy` (`strategy_code`, `strategy_name`, `strategy_type`, `description`, `author`, `parameters`) VALUES
('MA_CROSS_V1', '双均线交叉策略', 'TREND', '基于快慢均线交叉的趋势跟踪策略', 'ASTOCK Team', '{"fast_period": 10, "slow_period": 30, "take_profit": 0.15, "stop_loss": 0.05}'),
('MEAN_REVERT_V1', '均值回归策略', 'MEAN_REVERSION', '基于布林带的均值回归策略', 'ASTOCK Team', '{"boll_period": 20, "boll_std": 2.0, "position_size": 0.1}'),
('ALPHA_MOMENTUM_V1', '动量Alpha策略', 'ALPHA', '基于动量因子的选股策略', 'ASTOCK Team', '{"momentum_period": 60, "top_n": 10, "rebalance_days": 20}');

-- ============================================
-- 9. 恢复外键检查
-- ============================================
SET FOREIGN_KEY_CHECKS = 1;

-- ============================================
-- 脚本执行完成
-- ============================================
SELECT 'ASTOCK量化数据库初始化完成！' AS `message`;
SELECT NOW() AS `execution_time`;
SELECT COUNT(*) AS `tables_created` FROM information_schema.tables 
WHERE table_schema = 'astock_quant' AND table_type = 'BASE TABLE';