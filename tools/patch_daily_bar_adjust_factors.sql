ALTER TABLE `daily_bar`
    ADD COLUMN IF NOT EXISTS `pre_adjust_factor` DECIMAL(20, 8) DEFAULT NULL COMMENT '前复权因子' AFTER `circulating_market_cap`,
    ADD COLUMN IF NOT EXISTS `post_adjust_factor` DECIMAL(20, 8) DEFAULT NULL COMMENT '后复权因子' AFTER `pre_adjust_factor`;

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
    db.`turnover_rate`,
    db.`pre_adjust_factor`,
    db.`post_adjust_factor`
FROM `daily_bar` db
JOIN `symbol_info` si ON db.`symbol` = si.`symbol`
WHERE si.`status` = 'active';