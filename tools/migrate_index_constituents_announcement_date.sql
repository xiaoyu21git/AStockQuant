SET @schema_name = DATABASE();

SET @add_column_sql = IF(
    EXISTS(
        SELECT 1
        FROM INFORMATION_SCHEMA.COLUMNS
        WHERE TABLE_SCHEMA = @schema_name
          AND TABLE_NAME = 'index_constituents'
          AND COLUMN_NAME = 'announcement_date'
    ),
    'SELECT 1',
    'ALTER TABLE index_constituents ADD COLUMN announcement_date DATE NULL AFTER weight'
);
PREPARE stmt FROM @add_column_sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @add_index_sql = IF(
    EXISTS(
        SELECT 1
        FROM INFORMATION_SCHEMA.STATISTICS
        WHERE TABLE_SCHEMA = @schema_name
          AND TABLE_NAME = 'index_constituents'
          AND INDEX_NAME = 'idx_index_announcement_start'
    ),
    'SELECT 1',
    'ALTER TABLE index_constituents ADD INDEX idx_index_announcement_start (index_symbol, announcement_date, start_date)'
);
PREPARE stmt FROM @add_index_sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;