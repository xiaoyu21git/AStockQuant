-- ============================================
-- 数据清洗系统表（补充）
-- ============================================

-- 清洗任务记录表
CREATE TABLE IF NOT EXISTS `cleaning_tasks` (
    `task_id` INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '任务ID',
    `task_uuid` VARCHAR(36) NOT NULL COMMENT '任务UUID(唯一标识)',
    `symbol` VARCHAR(20) DEFAULT 'UNKNOWN' COMMENT '标的代码',
    `start_date` DATE DEFAULT NULL COMMENT '数据开始日期',
    `end_date` DATE DEFAULT NULL COMMENT '数据结束日期',
    `original_record_count` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '原始记录数',
    `cleaned_record_count` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '清洗后记录数',
    `removed_record_count` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '移除记录数',
    `data_quality_score` DECIMAL(8, 4) DEFAULT 0.0 COMMENT '数据质量评分(0-100)',
    `status` ENUM('PENDING', 'RUNNING', 'COMPLETED', 'FAILED', 'CANCELLED') DEFAULT 'COMPLETED' COMMENT '任务状态',
    `start_time` DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '任务开始时间',
    `end_time` DATETIME DEFAULT NULL COMMENT '任务结束时间',
    `duration_seconds` INT UNSIGNED DEFAULT NULL COMMENT '任务耗时(秒)',
    `error_message` TEXT DEFAULT NULL COMMENT '错误信息',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`task_id`),
    UNIQUE KEY `uk_task_uuid` (`task_uuid`),
    KEY `idx_symbol` (`symbol`),
    KEY `idx_status` (`status`),
    KEY `idx_created_at` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='清洗任务记录表';

-- 清洗结果数据表
CREATE TABLE IF NOT EXISTS `cleaning_results` (
    `result_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '结果ID',
    `task_id` INT UNSIGNED NOT NULL COMMENT '任务ID',
    `symbol` VARCHAR(20) NOT NULL COMMENT '标的代码',
    `trade_date` DATE NOT NULL COMMENT '交易日期',
    `open` DECIMAL(12, 4) DEFAULT NULL COMMENT '开盘价',
    `high` DECIMAL(12, 4) DEFAULT NULL COMMENT '最高价',
    `low` DECIMAL(12, 4) DEFAULT NULL COMMENT '最低价',
    `close` DECIMAL(12, 4) DEFAULT NULL COMMENT '收盘价',
    `volume` BIGINT UNSIGNED DEFAULT NULL COMMENT '成交量',
    `turnover` DECIMAL(20, 4) DEFAULT NULL COMMENT '成交额',
    `row_payload_json` LONGTEXT DEFAULT NULL COMMENT '完整行载荷JSON',
    `payload_version` INT UNSIGNED NOT NULL DEFAULT 1 COMMENT '行载荷版本',
    `is_cleaned` TINYINT(1) DEFAULT 1 COMMENT '是否清洗通过(0否1是)',
    `cleaning_notes` TEXT DEFAULT NULL COMMENT '清洗备注',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`result_id`),
    UNIQUE KEY `uk_task_symbol_date` (`task_id`, `symbol`, `trade_date`),
    KEY `fk_cleaning_task` (`task_id`),
    KEY `idx_symbol_date` (`symbol`, `trade_date`),
    KEY `idx_is_cleaned` (`is_cleaned`),
    CONSTRAINT `fk_cleaning_task` FOREIGN KEY (`task_id`) REFERENCES `cleaning_tasks` (`task_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='清洗结果数据表';

-- ============================================
-- 扩展回测数据表
-- ============================================

CREATE TABLE IF NOT EXISTS `news_sentiment` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '记录ID',
    `symbol` VARCHAR(20) NOT NULL DEFAULT 'MARKET' COMMENT '标的代码或市场级别标识',
    `trade_date` DATE NOT NULL COMMENT '归属交易日',
    `publish_time` DATETIME NOT NULL COMMENT '发布时间',
    `title` VARCHAR(500) DEFAULT NULL COMMENT '标题',
    `content` MEDIUMTEXT DEFAULT NULL COMMENT '内容摘要',
    `source` VARCHAR(100) DEFAULT NULL COMMENT '来源',
    `url` VARCHAR(500) DEFAULT NULL COMMENT '原文链接',
    `sentiment_score` DECIMAL(12, 6) DEFAULT NULL COMMENT '情绪分值',
    `market_sentiment` DECIMAL(12, 6) DEFAULT NULL COMMENT '市场情绪',
    `investor_sentiment` DECIMAL(12, 6) DEFAULT NULL COMMENT '投资者情绪',
    `sector_sentiment` DECIMAL(12, 6) DEFAULT NULL COMMENT '行业情绪',
    `theme_sentiment` DECIMAL(12, 6) DEFAULT NULL COMMENT '主题情绪',
    `social_sentiment` DECIMAL(12, 6) DEFAULT NULL COMMENT '社交情绪',
    `news_count` INT UNSIGNED NOT NULL DEFAULT 1 COMMENT '新闻计数',
    `title_hash` CHAR(32) NOT NULL COMMENT '标题哈希',
    `data_source` VARCHAR(100) DEFAULT NULL COMMENT '数据源',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_news_symbol_time_hash` (`symbol`, `publish_time`, `title_hash`),
    KEY `idx_news_trade_symbol` (`trade_date`, `symbol`),
    KEY `idx_news_publish_time` (`publish_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='新闻舆情扩展表';

CREATE TABLE IF NOT EXISTS `policy_data` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '记录ID',
    `symbol` VARCHAR(20) NOT NULL DEFAULT 'MARKET' COMMENT '标的代码或市场级别标识',
    `trade_date` DATE NOT NULL COMMENT '归属交易日',
    `publish_time` DATETIME NOT NULL COMMENT '发布时间',
    `title` VARCHAR(500) DEFAULT NULL COMMENT '标题',
    `content` MEDIUMTEXT DEFAULT NULL COMMENT '摘要',
    `policy_type` VARCHAR(100) DEFAULT NULL COMMENT '政策类型',
    `policy_score` DECIMAL(12, 6) DEFAULT NULL COMMENT '政策得分',
    `policy_strength` DECIMAL(12, 6) DEFAULT NULL COMMENT '政策强度',
    `policy_count` INT UNSIGNED NOT NULL DEFAULT 1 COMMENT '政策条数',
    `doc_hash` CHAR(32) NOT NULL COMMENT '文档哈希',
    `data_source` VARCHAR(100) DEFAULT NULL COMMENT '数据源',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_policy_symbol_time_hash` (`symbol`, `publish_time`, `doc_hash`),
    KEY `idx_policy_trade_symbol` (`trade_date`, `symbol`),
    KEY `idx_policy_publish_time` (`publish_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='政策与公告扩展表';

CREATE TABLE IF NOT EXISTS `alternative_data` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '记录ID',
    `symbol` VARCHAR(20) NOT NULL COMMENT '标的代码',
    `trade_date` DATE NOT NULL COMMENT '数据日期',
    `metric_type` VARCHAR(50) NOT NULL DEFAULT 'HOT_RANK' COMMENT '指标类型',
    `hot_rank` INT DEFAULT NULL COMMENT '热度排名',
    `popularity_score` DECIMAL(12, 6) DEFAULT NULL COMMENT '热度分值',
    `comment_count` INT UNSIGNED DEFAULT NULL COMMENT '评论数量',
    `comment_sentiment` DECIMAL(12, 6) DEFAULT NULL COMMENT '评论情绪',
    `extra_payload_json` LONGTEXT DEFAULT NULL COMMENT '原始扩展载荷',
    `data_source` VARCHAR(100) DEFAULT NULL COMMENT '数据源',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_alternative_symbol_date_metric` (`symbol`, `trade_date`, `metric_type`),
    KEY `idx_alternative_trade_symbol` (`trade_date`, `symbol`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='另类数据扩展表';

CREATE TABLE IF NOT EXISTS `derivatives_data` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '记录ID',
    `symbol` VARCHAR(20) NOT NULL DEFAULT 'MARKET' COMMENT '标的代码或市场级别标识',
    `underlying_symbol` VARCHAR(20) DEFAULT NULL COMMENT '对应现货/指数代码',
    `contract_code` VARCHAR(40) NOT NULL COMMENT '期货/衍生品合约代码',
    `trade_date` DATE NOT NULL COMMENT '数据日期',
    `futures_open` DECIMAL(16, 6) DEFAULT NULL COMMENT '期货开盘价',
    `futures_high` DECIMAL(16, 6) DEFAULT NULL COMMENT '期货最高价',
    `futures_low` DECIMAL(16, 6) DEFAULT NULL COMMENT '期货最低价',
    `futures_close` DECIMAL(16, 6) DEFAULT NULL COMMENT '期货收盘价',
    `futures_volume` DECIMAL(20, 4) DEFAULT NULL COMMENT '成交量',
    `open_interest` DECIMAL(20, 4) DEFAULT NULL COMMENT '持仓量',
    `basis` DECIMAL(16, 6) DEFAULT NULL COMMENT '基差',
    `basis_rate` DECIMAL(16, 6) DEFAULT NULL COMMENT '基差率',
    `data_source` VARCHAR(100) DEFAULT NULL COMMENT '数据源',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_derivatives_symbol_date_contract` (`symbol`, `trade_date`, `contract_code`),
    KEY `idx_derivatives_trade_symbol` (`trade_date`, `symbol`),
    KEY `idx_derivatives_underlying` (`underlying_symbol`, `trade_date`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='衍生品扩展表';
