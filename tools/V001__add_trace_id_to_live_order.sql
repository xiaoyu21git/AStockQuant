-- ============================================================================
-- V001: live_order 表添加 trace_id 列 — 跨日志关联追踪
-- PostgreSQL 版本
-- ============================================================================
-- traceId 从 StrategyEngine::step() 生成, 贯穿 信号→规则→提交→成交 全链路
-- 格式: UUID v4 (36 字符, 如 "xxxxxxxx-xxxx-4xxx-8xxx-xxxxxxxxxxxx")
-- ============================================================================

-- 正向迁移
ALTER TABLE data.live_order ADD COLUMN IF NOT EXISTS trace_id VARCHAR(36);
CREATE INDEX IF NOT EXISTS idx_live_order_trace_id ON data.live_order (trace_id);

-- 回滚 (注意: 必须先删索引再删列, 否则 DROP COLUMN 自动级联删索引后 DROP INDEX 报错)
-- DROP INDEX IF EXISTS data.idx_live_order_trace_id;
-- ALTER TABLE data.live_order DROP COLUMN IF EXISTS trace_id;
