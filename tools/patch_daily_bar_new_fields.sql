-- 为 daily_bar 添加 Baostock 新增字段 (ps_ratio, pcf_ratio, is_st)
-- 2026-06-16: Baostock 提供 psTTM/pcfNcfTTM/isST, 之前未入库

ALTER TABLE daily_bar
  ADD COLUMN IF NOT EXISTS ps_ratio DECIMAL(10,4) NULL COMMENT '市销率 PS-TTM (Baostock)',
  ADD COLUMN IF NOT EXISTS pcf_ratio DECIMAL(10,4) NULL COMMENT '市现率 PCF-TTM (Baostock)',
  ADD COLUMN IF NOT EXISTS is_st TINYINT(1) NULL DEFAULT 0 COMMENT 'ST标记 1=ST (Baostock)';

-- 更新视图
CREATE OR REPLACE VIEW v_daily_bar AS
SELECT * FROM daily_bar;
