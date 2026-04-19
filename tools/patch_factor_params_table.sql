-- Patch: ensure factor_params exists for FactorRepository strict schema checks
-- Usage:
--   mysql -u <user> -p astock_quant < tools/patch_factor_params_table.sql

USE `astock_quant`;

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
