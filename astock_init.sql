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
    `industry_code` VARCHAR(50) DEFAULT NULL COMMENT '所属行业代码',
    `list_date` DATE DEFAULT NULL COMMENT '上市日期',
    `delist_date` DATE DEFAULT NULL COMMENT '退市日期',
    `status` ENUM('ACTIVE', 'DELISTED', 'SUSPENDED', 'ST', '*ST') DEFAULT 'ACTIVE' COMMENT '状态',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`symbol_id`),
    UNIQUE KEY `uk_symbol` (`symbol`),
    KEY `idx_exchange` (`exchange`),
    KEY `idx_industry_code` (`industry_code`),
    KEY `idx_status` (`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='标的物基本信息表';

-- ============================================
-- 2. 市场数据表（核心数据）
-- ============================================

-- 日线行情数据表（按年份分区，适合大量数据）
CREATE TABLE IF NOT EXISTS `daily_bar` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '行情ID',
    `symbol` VARCHAR(20) NOT NULL COMMENT '标的代码，如: 600000.SH',
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
    `pe_ratio` DECIMAL(10, 4) DEFAULT NULL COMMENT '市盈率',
    `pb_ratio` DECIMAL(10, 4) DEFAULT NULL COMMENT '市净率',
    `market_cap` DECIMAL(20, 4) DEFAULT NULL COMMENT '总市值',
    `circulating_market_cap` DECIMAL(20, 4) DEFAULT NULL COMMENT '流通市值',
    `pre_adjust_factor` DECIMAL(20, 8) DEFAULT NULL COMMENT '前复权因子',
    `post_adjust_factor` DECIMAL(20, 8) DEFAULT NULL COMMENT '后复权因子',
    `data_source` VARCHAR(50) DEFAULT 'UNKNOWN' COMMENT '数据源',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_symbol_date` (`symbol`, `trade_date`),
    KEY `idx_symbol` (`symbol`),
    KEY `idx_trade_date` (`trade_date`),
    CONSTRAINT `fk_daily_bar_symbol` FOREIGN KEY (`symbol`) REFERENCES `symbol_info` (`symbol`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='日线行情数据表';

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

-- 资金流向日数据表
CREATE TABLE IF NOT EXISTS `money_flow_daily` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '记录ID',
    `symbol` VARCHAR(20) NOT NULL COMMENT '标的代码，如: 000001.SZ',
    `trade_date` DATE NOT NULL COMMENT '交易日期',
    `main_inflow` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '主力流入金额(元)',
    `main_outflow` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '主力流出金额(元)',
    `net_main_inflow` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '主力净流入(元)',
    `large_inflow` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '大单流入金额(元)',
    `large_outflow` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '大单流出金额(元)',
    `medium_inflow` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '中单流入金额(元)',
    `medium_outflow` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '中单流出金额(元)',
    `small_inflow` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '小单流入金额(元)',
    `small_outflow` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '小单流出金额(元)',
    `net_amount` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '净流入总额(元)',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_money_flow_symbol_date` (`symbol`, `trade_date`),
    KEY `idx_money_flow_date` (`trade_date`),
    KEY `idx_money_flow_symbol` (`symbol`),
    CONSTRAINT `fk_money_flow_symbol` FOREIGN KEY (`symbol`) REFERENCES `symbol_info` (`symbol`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='资金流向日数据表';

-- 龙虎榜每日上榜记录表
CREATE TABLE IF NOT EXISTS `dragon_tiger_list` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '记录ID',
    `symbol` VARCHAR(20) NOT NULL COMMENT '标的代码',
    `trade_date` DATE NOT NULL COMMENT '交易日期',
    `reason` VARCHAR(200) NOT NULL COMMENT '上榜原因',
    `buy_amount` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '买入金额(元)',
    `sell_amount` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '卖出金额(元)',
    `net_amount` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '净买入金额(元)',
    `buy_count` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '买入席位数量',
    `sell_count` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '卖出席位数量',
    `institution_buy` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '机构净买入(元)',
    `institution_sell` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '机构净卖出(元)',
    `turnover_rate` DECIMAL(8, 4) DEFAULT NULL COMMENT '当日换手率(%)',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`id`),
    KEY `idx_lhb_symbol_date` (`symbol`, `trade_date`),
    KEY `idx_lhb_date` (`trade_date`),
    CONSTRAINT `fk_lhb_symbol` FOREIGN KEY (`symbol`) REFERENCES `symbol_info` (`symbol`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='龙虎榜每日上榜记录表';

-- ============================================
-- 3. 财务数据表
-- ============================================

-- 财务指标表（季度/年度）
CREATE TABLE IF NOT EXISTS `financial_indicator` (
    `indicator_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '指标ID',
    `symbol_id` INT UNSIGNED NOT NULL COMMENT '标的ID',
    `report_date` DATE NOT NULL COMMENT '报告期',
    `report_type` ENUM('Q1', 'Q2', 'Q3', 'Q4', 'FY') NOT NULL COMMENT '报告类型',
    `effective_disclosure_date` DATE DEFAULT NULL COMMENT '实际披露日期',
    `eps` DECIMAL(10, 4) DEFAULT NULL COMMENT '每股收益',
    `bps` DECIMAL(10, 4) DEFAULT NULL COMMENT '每股净资产',
    `roa` DECIMAL(8, 4) DEFAULT NULL COMMENT '总资产收益率',
    `roe` DECIMAL(8, 4) DEFAULT NULL COMMENT '净资产收益率',
    `profit_margin` DECIMAL(8, 4) DEFAULT NULL COMMENT '净利率',
    `gross_margin` DECIMAL(8, 4) DEFAULT NULL COMMENT '毛利率',
    `operating_margin` DECIMAL(8, 4) DEFAULT NULL COMMENT '营业利润率',
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
    `dividend_yield` DECIMAL(12, 6) DEFAULT NULL COMMENT '股息率',
    `payout_ratio` DECIMAL(12, 6) DEFAULT NULL COMMENT '分红支付率',
    `dividend_stability` DECIMAL(8, 4) DEFAULT NULL COMMENT '分红稳定性',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`indicator_id`),
    UNIQUE KEY `uk_symbol_report` (`symbol_id`, `report_date`, `report_type`),
    KEY `idx_symbol_id` (`symbol_id`),
    KEY `idx_report_date` (`report_date`),
    CONSTRAINT `fk_financial_symbol` FOREIGN KEY (`symbol_id`) REFERENCES `symbol_info` (`symbol_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='财务指标表';

-- 财务指标日频对齐表（按交易日展开到最新可用财报）
CREATE TABLE IF NOT EXISTS `financial_indicator_daily` (
    `indicator_daily_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '日频指标ID',
    `symbol_id` INT UNSIGNED NOT NULL COMMENT '标的ID',
    `trade_date` DATE NOT NULL COMMENT '交易日期',
    `report_date` DATE NOT NULL COMMENT '原始报告期',
    `report_type` ENUM('Q1', 'Q2', 'Q3', 'Q4', 'FY') NOT NULL COMMENT '原始报告类型',
    `effective_disclosure_date` DATE DEFAULT NULL COMMENT '实际披露日期',
    `eps` DECIMAL(10, 4) DEFAULT NULL COMMENT '每股收益',
    `bps` DECIMAL(10, 4) DEFAULT NULL COMMENT '每股净资产',
    `roa` DECIMAL(8, 4) DEFAULT NULL COMMENT '总资产收益率',
    `roe` DECIMAL(8, 4) DEFAULT NULL COMMENT '净资产收益率',
    `profit_margin` DECIMAL(8, 4) DEFAULT NULL COMMENT '净利率',
    `gross_margin` DECIMAL(8, 4) DEFAULT NULL COMMENT '毛利率',
    `operating_margin` DECIMAL(8, 4) DEFAULT NULL COMMENT '营业利润率',
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
    `dividend_yield` DECIMAL(12, 6) DEFAULT NULL COMMENT '股息率',
    `payout_ratio` DECIMAL(12, 6) DEFAULT NULL COMMENT '分红支付率',
    `dividend_stability` DECIMAL(8, 4) DEFAULT NULL COMMENT '分红稳定性',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`indicator_daily_id`),
    UNIQUE KEY `uk_symbol_trade_date` (`symbol_id`, `trade_date`),
    KEY `idx_symbol_trade_date` (`symbol_id`, `trade_date`),
    KEY `idx_trade_date` (`trade_date`),
    KEY `idx_report_date` (`report_date`),
    CONSTRAINT `fk_financial_daily_symbol` FOREIGN KEY (`symbol_id`) REFERENCES `symbol_info` (`symbol_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='财务指标日频对齐表';

-- ============================================
-- 4. 策略与回测系统表
-- ============================================

-- 策略定义表
CREATE TABLE IF NOT EXISTS `strategy` (
    `strategy_id`           VARCHAR(128) NOT NULL COMMENT '策略UUID主键',
    `engine_strategy_id`    BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '策略引擎数值ID',
    `strategy_code`         VARCHAR(128) NOT NULL COMMENT '策略代码(唯一标识)',
    `metadata_json`         JSON NOT NULL COMMENT '策略元数据(名称/描述/behaviorKind/factorIds/ruleIds/enabled/uuid)',
    `strategy_identity_json` JSON NOT NULL COMMENT '策略身份(storedTypeIndex/behaviorKind)',
    `version`               VARCHAR(64) NULL COMMENT '策略版本',
    `author`                VARCHAR(128) NULL COMMENT '作者',
    `language`              VARCHAR(32) NOT NULL DEFAULT 'PYTHON' COMMENT '实现语言',
    `status`                VARCHAR(32) NOT NULL DEFAULT 'ACTIVE' COMMENT '生命周期状态',
    `created_at`            DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at`            DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    `parameters`            JSON NULL COMMENT '策略参数(JSON)',
    `performance_metrics`   JSON NULL COMMENT '绩效指标(JSON)',
    `runtime_json`          JSON NULL COMMENT '运行时属性(assetTypeIndex/timeFrameIndex/riskLevelIndex)',
    PRIMARY KEY (`strategy_id`),
    UNIQUE KEY `uq_strategy_code` (`strategy_code`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='策略定义表';

-- 回测配置表
CREATE TABLE IF NOT EXISTS `backtest_config` (
    `config_id` INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '配置ID',
    `config_name` VARCHAR(200) NOT NULL COMMENT '配置名称',
    `strategy_id` VARCHAR(100) NOT NULL COMMENT '策略ID',
    `start_date` DATE NOT NULL COMMENT '回测开始日期',
    `end_date` DATE NOT NULL COMMENT '回测结束日期',
    `initial_capital` DECIMAL(15, 4) NOT NULL DEFAULT 1000000.0 COMMENT '初始资金',
    `benchmark` VARCHAR(20) DEFAULT '000300.SH' COMMENT '基准指数',
    `commission_rate` DECIMAL(8, 6) DEFAULT 0.0003 COMMENT '佣金费率',
    `slippage_rate` DECIMAL(8, 6) DEFAULT 0.0001 COMMENT '滑点费率',
    `parameters` TEXT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '策略参数(JSON格式，TEXT类型兼容Qt驱动)',
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
-- 实盘交易持久化表
-- ============================================

-- 实盘订单表 (订单+成交合为一张, 一行一条订单, 成交后更新)
CREATE TABLE IF NOT EXISTS `live_order` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `cl_ord_id` VARCHAR(64) NOT NULL COMMENT '客户端订单ID',
    `strategy_id` VARCHAR(64) NOT NULL COMMENT '策略ID',
    `symbol` VARCHAR(16) NOT NULL COMMENT '标的代码',
    `side` ENUM('BUY','SELL') NOT NULL COMMENT '买卖方向',
    `order_type` ENUM('LIMIT','MARKET') NOT NULL DEFAULT 'LIMIT',
    `price` DECIMAL(12,4) NOT NULL COMMENT '委托价格',
    `quantity` INT NOT NULL COMMENT '委托数量(股)',
    `signal_score` DECIMAL(6,4) DEFAULT 0.0 COMMENT '信号强度',
    `position_effect` ENUM('OPEN','CLOSE') NOT NULL DEFAULT 'OPEN' COMMENT '开平仓',
    `trading_day` INT NOT NULL COMMENT '交易日 YYYYMMDD',
    `status` ENUM('PENDING','PARTIAL_FILLED','FILLED','CANCELLED','REJECTED') NOT NULL DEFAULT 'PENDING' COMMENT '订单状态',
    `broker_order_id` VARCHAR(64) DEFAULT NULL COMMENT '券商订单ID',
    `message` VARCHAR(500) DEFAULT NULL COMMENT '状态消息/拒绝原因',
    -- 成交字段 (收到成交回报后更新)
    `exec_id` VARCHAR(64) DEFAULT NULL COMMENT '交易所成交编号',
    `fill_price` DECIMAL(12,4) DEFAULT NULL COMMENT '成交价格',
    `fill_qty` INT DEFAULT NULL COMMENT '成交数量(股)',
    `fill_amount` DECIMAL(15,4) DEFAULT NULL COMMENT '成交金额',
    `commission` DECIMAL(10,4) DEFAULT NULL COMMENT '手续费',
    `fill_time` DATETIME DEFAULT NULL COMMENT '成交时间',
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_cl_ord_id` (`cl_ord_id`),
    KEY `idx_strategy_day` (`strategy_id`, `trading_day`),
    KEY `idx_symbol_day` (`symbol`, `trading_day`),
    KEY `idx_status` (`status`),
    KEY `idx_created_at` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='实盘订单表(含成交)';

-- 实盘每日账户快照表
CREATE TABLE IF NOT EXISTS `live_account_daily` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `trading_day` INT NOT NULL COMMENT '交易日 YYYYMMDD',
    `total_asset` DECIMAL(15,4) NOT NULL COMMENT '总资产',
    `available_cash` DECIMAL(15,4) NOT NULL COMMENT '可用资金',
    `market_value` DECIMAL(15,4) NOT NULL COMMENT '持仓市值',
    `frozen_cash` DECIMAL(15,4) DEFAULT 0.0 COMMENT '冻结资金',
    `realized_pnl` DECIMAL(15,4) DEFAULT 0.0 COMMENT '已实现盈亏',
    `unrealized_pnl` DECIMAL(15,4) DEFAULT 0.0 COMMENT '未实现盈亏',
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_trading_day` (`trading_day`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='实盘每日账户快照';

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
    db.`id` AS `bar_id`,
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
JOIN `symbol_info` si ON db.`symbol` = si.`symbol`
WHERE si.`status` = 'active';

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
INSERT IGNORE INTO `symbol_info` (`symbol`, `name`, `exchange`, `industry_code`, `list_date`) VALUES
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
('MEAN_REVERT_V1', '均值回归策略', 'MEAN_REVERSION', '基于布林带的均值回归策略', 'ASTOCK Team', '{"boll_period": 20, "boll_std": 2.0, "positionSize": 0.1}'),
('ALPHA_MOMENTUM_V1', '动量Alpha策略', 'ALPHA', '基于动量因子的选股策略', 'ASTOCK Team', '{"momentum_period": 60, "topN": 10, "rebalance_days": 20}');

-- ============================================
-- 9. 恢复外键检查
-- ============================================
SET FOREIGN_KEY_CHECKS = 1;

-- ============================================
-- 10. 因子系统表
-- ============================================

-- 因子定义表
CREATE TABLE IF NOT EXISTS `factors` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '自增主键',
    `factor_id` VARCHAR(100) NOT NULL COMMENT '因子唯一标识(业务ID)',
    `factor_name` VARCHAR(100) NOT NULL COMMENT '因子名称(英文标识)',
    `display_name` VARCHAR(200) NOT NULL COMMENT '因子显示名称',
    `major_category` VARCHAR(50) NOT NULL COMMENT '主类别(价值/动量/质量等)',
    `sub_category` VARCHAR(50) DEFAULT NULL COMMENT '子类别',
    `description` TEXT DEFAULT NULL COMMENT '因子描述',
    `ic_value` DECIMAL(8, 4) DEFAULT 0.0 COMMENT 'IC值(-1到1)',
    `ir_value` DECIMAL(8, 4) DEFAULT 0.0 COMMENT 'IR值(信息比率)',
    `validity_days` INT UNSIGNED DEFAULT 30 COMMENT '有效天数(1-365)',
    `actual_start_date` DATE DEFAULT NULL COMMENT '回测实际起始日',
    `effective_start_date` DATE DEFAULT NULL COMMENT '有效期起始日',
    `effective_end_date` DATE DEFAULT NULL COMMENT '有效期结束日',
    `warmup_trimmed_trading_days` INT UNSIGNED DEFAULT 0 COMMENT '预热裁剪交易日数',
    `turnover_rate` DECIMAL(8, 4) DEFAULT 0.25 COMMENT '换手率(0-1)',
    `is_recommended` TINYINT(1) DEFAULT 0 COMMENT '是否推荐(0否1是)',
    `is_favorite` TINYINT(1) DEFAULT 0 COMMENT '是否收藏(0否1是)',
    `status` ENUM('active', 'inactive', 'deprecated') DEFAULT 'active' COMMENT '因子状态',
    `creator` VARCHAR(50) DEFAULT 'system' COMMENT '创建者',
    `create_date` DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '创建日期',
    `update_date` DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新日期',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_factor_id` (`factor_id`),
    KEY `idx_factor_name` (`factor_name`),
    KEY `idx_major_category` (`major_category`),
    KEY `idx_sub_category` (`sub_category`),
    KEY `idx_status` (`status`),
    KEY `idx_is_favorite` (`is_favorite`),
    KEY `idx_create_date` (`create_date`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='因子定义表';

-- 因子标签关联表
CREATE TABLE IF NOT EXISTS `factor_tags` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '自增主键',
    `factor_id` VARCHAR(100) NOT NULL COMMENT '因子ID',
    `tag` VARCHAR(50) NOT NULL COMMENT '标签名称',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_factor_tag` (`factor_id`, `tag`),
    KEY `idx_factor_id` (`factor_id`),
    KEY `idx_tag` (`tag`),
    CONSTRAINT `fk_factor_tags_factor` FOREIGN KEY (`factor_id`) REFERENCES `factors` (`factor_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='因子标签关联表';

-- 因子参数表
CREATE TABLE IF NOT EXISTS `factor_params` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '自增主键',
    `factor_id` VARCHAR(100) NOT NULL COMMENT '因子ID',
    `param_name` VARCHAR(100) NOT NULL COMMENT '参数名称',
    `param_display_name` VARCHAR(200) DEFAULT NULL COMMENT '参数显示名称',
    `param_type` ENUM('integer', 'float', 'boolean', 'string', 'enum', 'array', 'object') NOT NULL DEFAULT 'string' COMMENT '参数类型',
    `param_value` TEXT NOT NULL COMMENT '参数值(JSON格式存储)',
    `default_value` TEXT DEFAULT NULL COMMENT '默认值(JSON格式)',
    `min_value` TEXT DEFAULT NULL COMMENT '最小值',
    `max_value` TEXT DEFAULT NULL COMMENT '最大值',
    `step_value` TEXT DEFAULT NULL COMMENT '步长',
    `options` TEXT DEFAULT NULL COMMENT '枚举选项(JSON数组)',
    `description` TEXT DEFAULT NULL COMMENT '参数描述',
    `is_required` TINYINT(1) DEFAULT 0 COMMENT '是否必填',
    `param_order` INT DEFAULT 0 COMMENT '参数顺序',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_factor_param` (`factor_id`, `param_name`),
    KEY `idx_factor_id` (`factor_id`),
    KEY `idx_param_name` (`param_name`),
    CONSTRAINT `fk_factor_params_factor` FOREIGN KEY (`factor_id`) REFERENCES `factors` (`factor_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='因子参数表';

-- 因子性能指标历史表
CREATE TABLE IF NOT EXISTS `factor_performance_history` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '自增主键',
    `factor_id` VARCHAR(100) NOT NULL COMMENT '因子ID',
    `calc_date` DATE NOT NULL COMMENT '计算日期',
    `ic_value` DECIMAL(8, 4) DEFAULT NULL COMMENT 'IC值',
    `ir_value` DECIMAL(8, 4) DEFAULT NULL COMMENT 'IR值',
    `ic_positive_ratio` DECIMAL(8, 4) DEFAULT NULL COMMENT 'IC正比例',
    `long_short_return` DECIMAL(10, 4) DEFAULT NULL COMMENT '多空收益',
    `group_returns` JSON DEFAULT NULL COMMENT '分组收益(JSON数组)',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_factor_date` (`factor_id`, `calc_date`),
    KEY `idx_factor_id` (`factor_id`),
    KEY `idx_calc_date` (`calc_date`),
    CONSTRAINT `fk_factor_perf_factor` FOREIGN KEY (`factor_id`) REFERENCES `factors` (`factor_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='因子性能指标历史表';

-- 插入示例因子数据
INSERT IGNORE INTO `factors` (`factor_id`, `factor_name`, `display_name`, `major_category`, `sub_category`, `description`, `ic_value`, `ir_value`, `validity_days`, `turnover_rate`, `is_recommended`, `status`, `creator`) VALUES
('value_pe_ttm_001', 'pe_ttm_factor', '市盈率TTM因子', '价值因子', '估值', '基于滚动12个月市盈率构建的价值因子，低PE表示估值便宜', 0.05, 1.2, 30, 0.25, 1, 'active', 'system'),
('momentum_60d_001', 'momentum_60d', '60日动量因子', '动量因子', '趋势动量', '基于过去60个交易日累计收益率构建的动量因子', 0.08, 1.5, 20, 0.40, 1, 'active', 'system'),
('quality_roe_001', 'quality_roe', 'ROE质量因子', '质量因子', '盈利能力', '基于净资产收益率ROE构建的质量因子，高ROE表示盈利能力强', 0.06, 1.3, 90, 0.20, 1, 'active', 'system'),
('growth_composite_001', 'growth_composite', '成长组合因子', '成长因子', '成长组合', '基于营收增速、单季净利同比增速、DELTAROE和SUE构建的成长组合因子', 0.07, 1.4, 120, 0.28, 1, 'active', 'system'),
('size_market_cap_001', 'market_cap_factor', '市值规模因子', '规模因子', '市值规模', '基于总市值构建的规模因子，取对数后标准化', -0.03, 0.8, 60, 0.15, 0, 'active', 'system'),
('low_vol_20d_001', 'low_volatility_20d', '20日低波动因子', '低波因子', '波动率', '基于过去20个交易日收益率标准差构建的低波动因子', 0.04, 1.1, 20, 0.30, 0, 'active', 'system');

-- 插入示例因子标签
INSERT IGNORE INTO `factor_tags` (`factor_id`, `tag`) VALUES
('value_pe_ttm_001', '价值'),
('value_pe_ttm_001', '估值'),
('value_pe_ttm_001', '基本面'),
('momentum_60d_001', '动量'),
('momentum_60d_001', '趋势'),
('momentum_60d_001', '技术指标'),
('quality_roe_001', '质量'),
('quality_roe_001', '盈利'),
('quality_roe_001', '财务'),
('growth_composite_001', '成长'),
('growth_composite_001', '成长性'),
('growth_composite_001', '组合'),
('size_market_cap_001', '规模'),
('size_market_cap_001', '市值'),
('low_vol_20d_001', '低波'),
('low_vol_20d_001', '风险'),
('low_vol_20d_001', '稳定性');

-- 插入示例因子参数
INSERT IGNORE INTO `factor_params` (`factor_id`, `param_name`, `param_display_name`, `param_type`, `param_value`, `default_value`, `min_value`, `max_value`, `description`, `is_required`, `param_order`) VALUES
('value_pe_ttm_001', 'percentile_threshold', '分位数阈值', 'integer', '30', '30', '0', '100', '价值因子的分位数阈值(%)', 0, 1),
('value_pe_ttm_001', 'weighting', '权重分配', 'enum', '"等权重"', '"等权重"', NULL, NULL, '估值指标的权重分配方法', 0, 2),
('momentum_60d_001', 'lookback_window', '动量窗口', 'integer', '60', '60', '5', '250', '计算动量的时间窗口(天数)', 1, 1),
('momentum_60d_001', 'skip_recent', '跳过近期', 'integer', '20', '20', '0', '60', '跳过最近N天数据(避免反转效应)', 0, 2),
('momentum_60d_001', 'method', '计算方法', 'enum', '"简单动量"', '"简单动量"', NULL, NULL, '动量计算方法', 0, 3),
('quality_roe_001', 'stability_window', '稳定性窗口', 'integer', '8', '8', '4', '20', '评估指标稳定性的时间窗口(季度)', 0, 1),
('growth_composite_001', 'growth_metrics', '成长指标', 'array', '["revenue_growth","net_profit_growth","delta_roe","sue"]', '["revenue_growth","net_profit_growth","delta_roe","sue"]', NULL, NULL, '参与成长组合的指标', 1, 1),
('growth_composite_001', 'revenue_growth_weight', '营收增速权重', 'integer', '25', '25', '0', '100', '营收增速在成长组合中的权重', 0, 2),
('growth_composite_001', 'net_profit_growth_weight', '单季净利同比增速权重', 'integer', '25', '25', '0', '100', '单季净利同比增速在成长组合中的权重', 0, 3),
('growth_composite_001', 'delta_roe_weight', 'DELTAROE权重', 'integer', '25', '25', '0', '100', 'DELTAROE（ROE同比变化）在成长组合中的权重', 0, 4),
('growth_composite_001', 'sue_weight', 'SUE权重', 'integer', '25', '25', '0', '100', 'SUE（标准化预期外盈利）在成长组合中的权重', 0, 5),
('size_market_cap_001', 'log_transform', '对数变换', 'boolean', 'true', 'true', NULL, NULL, '是否对规模指标进行对数变换', 0, 1),
('low_vol_20d_001', 'volatility_window', '波动率窗口', 'integer', '20', '20', '5', '120', '计算波动率的时间窗口(天数)', 1, 1),
('low_vol_20d_001', 'volatility_type', '波动率类型', 'enum', '"历史波动率"', '"历史波动率"', NULL, NULL, '波动率计算方法', 0, 2);

-- ============================================
-- 脚本执行完成
-- ============================================
SELECT 'ASTOCK量化数据库初始化完成！' AS `message`;
SELECT NOW() AS `execution_time`;
SELECT COUNT(*) AS `tables_created` FROM information_schema.tables 
WHERE table_schema = 'astock_quant' AND table_type = 'BASE TABLE';
