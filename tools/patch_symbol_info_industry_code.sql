ALTER TABLE `symbol_info`
    ADD COLUMN IF NOT EXISTS `industry_code` VARCHAR(50) DEFAULT NULL COMMENT '所属行业代码' AFTER `asset_class`;

SET @has_legacy_industry := (
    SELECT COUNT(*)
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'symbol_info'
      AND COLUMN_NAME = 'industry'
);

SET @backfill_sql := IF(
    @has_legacy_industry > 0,
    'UPDATE `symbol_info` SET `industry_code` = `industry` WHERE (`industry_code` IS NULL OR TRIM(`industry_code`) = '''') AND `industry` IS NOT NULL AND TRIM(`industry`) <> '''';',
    'SELECT ''symbol_info.industry legacy column not found, skip backfill'' AS message;'
);

PREPARE patch_stmt FROM @backfill_sql;
EXECUTE patch_stmt;
DEALLOCATE PREPARE patch_stmt;

SELECT COLUMN_NAME, COLUMN_TYPE, IS_NULLABLE
FROM information_schema.COLUMNS
WHERE TABLE_SCHEMA = DATABASE()
  AND TABLE_NAME = 'symbol_info'
  AND COLUMN_NAME IN ('industry', 'industry_code')
ORDER BY COLUMN_NAME;