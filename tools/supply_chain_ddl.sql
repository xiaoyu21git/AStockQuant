-- ══════════════════════════════════════════════════════════════════════════════
-- 传导链因子 (SupplyChainFactor) 数据库表 DDL
-- 执行: psql -h 127.0.0.1 -U astock -d astock_quant -f tools/supply_chain_ddl.sql
-- ══════════════════════════════════════════════════════════════════════════════

-- 1. 商品→股票映射关系表 (ref schema)
CREATE TABLE IF NOT EXISTS ref.product_stock_mapping (
    id              SERIAL PRIMARY KEY,
    product_id      VARCHAR(32)  NOT NULL,
    symbol          VARCHAR(16)  NOT NULL,
    weight          DECIMAL(10,4) DEFAULT 1.0,
    effective_date  DATE         NOT NULL,
    expired_date    DATE         DEFAULT '2099-12-31',
    version         VARCHAR(32)  DEFAULT 'ckg_v1',
    UNIQUE(product_id, symbol, effective_date)
);

COMMENT ON TABLE ref.product_stock_mapping IS '商品→A股映射关系（传导链因子核心映射表）';
COMMENT ON COLUMN ref.product_stock_mapping.product_id IS '商品标识（如 lithium_carbonate, copper）';
COMMENT ON COLUMN ref.product_stock_mapping.effective_date IS '映射生效日期（防未来函数核心字段）';
COMMENT ON COLUMN ref.product_stock_mapping.expired_date IS '映射失效日期';
COMMENT ON COLUMN ref.product_stock_mapping.version IS '数据源版本标识';

-- 2. 商品价格日表 (mkt schema)
CREATE TABLE IF NOT EXISTS mkt.commodity_prices_daily (
    product_id  VARCHAR(32)     NOT NULL,
    trade_date  DATE            NOT NULL,
    close_price DECIMAL(20,4)   NOT NULL,
    PRIMARY KEY (product_id, trade_date)
);

CREATE INDEX IF NOT EXISTS idx_cpd_product_date
    ON mkt.commodity_prices_daily(product_id, trade_date);

COMMENT ON TABLE mkt.commodity_prices_daily IS '商品每日收盘价（传导链因子价格源）';

-- 3. 动态评分排名表 (alpha schema)
CREATE TABLE IF NOT EXISTS alpha.commodity_daily_rank (
    calc_date   DATE            NOT NULL,
    product_id  VARCHAR(32)     NOT NULL,
    rank_num    INT             NOT NULL,
    score       NUMERIC(10,4)   NOT NULL,
    PRIMARY KEY (calc_date, rank_num)
);

COMMENT ON TABLE alpha.commodity_daily_rank IS '商品日排名（传导链因子Top-N选择源）';
COMMENT ON COLUMN alpha.commodity_daily_rank.score IS '综合评分（动量×0.5 + 库存偏离×0.3 + 景气×0.2）';
