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
    `open` DECIMAL(12, 4) NOT NULL COMMENT '开盘价',
    `high` DECIMAL(12, 4) NOT NULL COMMENT '最高价',
    `low` DECIMAL(12, 4) NOT NULL COMMENT '最低价',
    `close` DECIMAL(12, 4) NOT NULL COMMENT '收盘价',
    `volume` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '成交量',
    `turnover` DECIMAL(20, 4) NOT NULL DEFAULT 0.0 COMMENT '成交额',
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
