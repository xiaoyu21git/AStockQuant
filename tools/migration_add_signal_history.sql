-- ============================================================================
-- 迁移脚本: 创建 live.signal_history 表
-- 版本: v0.16.0
-- 功能: 信号模式运行时的信号持久化存储, 按交易日归档
-- 执行: psql -U <user> -d <dbname> -f tools/migration_add_signal_history.sql
-- ============================================================================

-- 信号历史表
CREATE TABLE IF NOT EXISTS live.signal_history (
    signal_id       UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    strategy_id     UUID,                               -- 策略 ID (Phase 2: 添加 FK)
    trading_day     DATE NOT NULL,                      -- 交易日 (北京时间, 按天归档)
    symbol          VARCHAR(20) NOT NULL,               -- 完整代码 (如 "600001.SH")
    full_symbol     VARCHAR(30) NOT NULL,               -- 完整代码 (同上)
    intent          SMALLINT NOT NULL DEFAULT 0,        -- SignalIntent: 0=KEEP,1=OPEN,2=ADD,3=REDUCE,4=CLOSE
    target_weight   DECIMAL(10, 4) DEFAULT 0.0,         -- 目标权重
    score           DECIMAL(10, 4) DEFAULT 0.0,         -- 因子得分
    signal_format   VARCHAR(20) DEFAULT 'internal',     -- 格式: 'internal' | 'ths' | 'tdx'
    push_target     VARCHAR(20) DEFAULT 'file',         -- 推送目标: 'file' | 'socket' | 'wechat'
    signal_time     TIMESTAMPTZ NOT NULL DEFAULT NOW(), -- 信号时间 (UTC)
    trace_id        VARCHAR(64),                        -- TraceID 跨日志关联
    pushed          BOOLEAN DEFAULT FALSE,              -- 是否推送成功
    push_error      TEXT DEFAULT '',                    -- 推送失败原因 (空=成功)
    created_at      TIMESTAMPTZ DEFAULT NOW()           -- 记录创建时间
);

-- 按策略+日期查询历史 (最常用查询)
CREATE INDEX IF NOT EXISTS idx_signal_history_strategy_date
    ON live.signal_history(strategy_id, trading_day DESC);

-- 按交易日批量查询
CREATE INDEX IF NOT EXISTS idx_signal_history_trading_day
    ON live.signal_history(trading_day DESC);

-- 按 trace_id 查询 (调试用)
CREATE INDEX IF NOT EXISTS idx_signal_history_trace_id
    ON live.signal_history(trace_id);

-- 添加注释
COMMENT ON TABLE live.signal_history IS 'v0.16.0 信号历史记录表 - 信号模式运行时按交易日归档';
COMMENT ON COLUMN live.signal_history.trading_day IS '北京时间日期 (YYYY-MM-DD), 从 UTC signal_time 转换';
COMMENT ON COLUMN live.signal_history.intent IS 'SignalIntent: 0=KEEP, 1=OPEN, 2=ADD, 3=REDUCE, 4=CLOSE';
COMMENT ON COLUMN live.signal_history.signal_format IS '信号格式化器: internal, ths, tdx';
COMMENT ON COLUMN live.signal_history.push_target IS '推送目标: file, socket, wechat';
COMMENT ON COLUMN live.signal_history.pushed IS '推送是否成功 (无论成败信号本身都持久化)';
COMMENT ON COLUMN live.signal_history.push_error IS '推送失败原因 (空=成功或未推送)';
