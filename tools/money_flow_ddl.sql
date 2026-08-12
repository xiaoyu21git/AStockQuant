-- 资金流日频数据表
-- TimescaleDB hypertable, 按 trade_date 分区, 按 symbol_id 压缩段
-- 存储掘金 stk_get_money_flow 返回的完整 StkMoneyFlowRecord 字段

CREATE TABLE IF NOT EXISTS fund.money_flow_daily (
    symbol_id        BIGINT NOT NULL,
    trade_date       DATE NOT NULL,
    -- 主力资金
    main_in          DOUBLE PRECISION,
    main_out         DOUBLE PRECISION,
    main_net_in      DOUBLE PRECISION,
    main_net_in_rate DOUBLE PRECISION,
    -- 超大单资金
    super_in         DOUBLE PRECISION,
    super_out        DOUBLE PRECISION,
    super_net_in     DOUBLE PRECISION,
    super_net_in_rate DOUBLE PRECISION,
    -- 大单资金（残差资金流强度因子依赖）
    large_in         DOUBLE PRECISION,
    large_out        DOUBLE PRECISION,
    large_net_in     DOUBLE PRECISION,
    large_net_in_rate DOUBLE PRECISION,
    -- 中单资金
    mid_in           DOUBLE PRECISION,
    mid_out          DOUBLE PRECISION,
    mid_net_in       DOUBLE PRECISION,
    mid_net_in_rate  DOUBLE PRECISION,
    -- 小单资金
    small_in         DOUBLE PRECISION,
    small_out        DOUBLE PRECISION,
    small_net_in     DOUBLE PRECISION,
    small_net_in_rate DOUBLE PRECISION,
    UNIQUE (symbol_id, trade_date)
);

-- 顺序: 先 create_hypertable → 再 SET compress (TimescaleDB 压缩只支持超表)
SELECT create_hypertable('fund.money_flow_daily', 'trade_date',
    chunk_time_interval => INTERVAL '1 year');

ALTER TABLE fund.money_flow_daily SET (
    timescaledb.compress,
    timescaledb.compress_segmentby = 'symbol_id'
);
SELECT add_compression_policy('fund.money_flow_daily', INTERVAL '30 days');
