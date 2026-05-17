-- PostgreSQL 目标库结构草案
-- 说明：这是实施起点，不是最终生产版。
-- 目标：先把核心 schema 和关键表骨架固定下来。

CREATE SCHEMA IF NOT EXISTS core;
CREATE SCHEMA IF NOT EXISTS market;
CREATE SCHEMA IF NOT EXISTS factor;
CREATE SCHEMA IF NOT EXISTS backtest;
CREATE SCHEMA IF NOT EXISTS cleaning;
CREATE SCHEMA IF NOT EXISTS archive;

-- =========================
-- core.symbol_info
-- =========================
CREATE TABLE IF NOT EXISTS core.symbol_info (
    symbol_id BIGSERIAL PRIMARY KEY,
    symbol VARCHAR(20) NOT NULL UNIQUE,
    name VARCHAR(100) NOT NULL,
    exchange VARCHAR(10) NOT NULL,
    asset_class VARCHAR(20) NOT NULL DEFAULT 'STOCK',
    industry VARCHAR(50),
    list_date DATE,
    delist_date DATE,
    status VARCHAR(20) NOT NULL DEFAULT 'ACTIVE',
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_symbol_info_exchange ON core.symbol_info(exchange);
CREATE INDEX IF NOT EXISTS idx_symbol_info_industry_code ON core.symbol_info(industry_code);
CREATE INDEX IF NOT EXISTS idx_symbol_info_status ON core.symbol_info(status);

-- =========================
-- market.daily_bar
-- =========================
CREATE TABLE IF NOT EXISTS market.daily_bar (
    symbol_id BIGINT NOT NULL REFERENCES core.symbol_info(symbol_id) ON DELETE CASCADE,
    trade_date DATE NOT NULL,
    open NUMERIC(18, 6) NOT NULL,
    high NUMERIC(18, 6) NOT NULL,
    low NUMERIC(18, 6) NOT NULL,
    close NUMERIC(18, 6) NOT NULL,
    pre_close NUMERIC(18, 6),
    volume BIGINT NOT NULL DEFAULT 0,
    turnover NUMERIC(24, 6) NOT NULL DEFAULT 0,
    change_pct NUMERIC(12, 6),
    change_amt NUMERIC(18, 6),
    amplitude NUMERIC(12, 6),
    turnover_rate NUMERIC(12, 6),
    pe_ratio NUMERIC(18, 6),
    pb_ratio NUMERIC(18, 6),
    market_cap NUMERIC(24, 6),
    circulating_market_cap NUMERIC(24, 6),
    data_source VARCHAR(50) NOT NULL DEFAULT 'UNKNOWN',
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (symbol_id, trade_date)
);

CREATE INDEX IF NOT EXISTS idx_daily_bar_trade_date ON market.daily_bar(trade_date);
CREATE INDEX IF NOT EXISTS idx_daily_bar_symbol ON market.daily_bar(symbol_id);

-- =========================
-- market.minute_bar
-- =========================
CREATE TABLE IF NOT EXISTS market.minute_bar (
    bar_id BIGSERIAL PRIMARY KEY,
    symbol_id BIGINT NOT NULL REFERENCES core.symbol_info(symbol_id) ON DELETE CASCADE,
    timeframe VARCHAR(10) NOT NULL,
    bar_time TIMESTAMPTZ NOT NULL,
    open NUMERIC(18, 6) NOT NULL,
    high NUMERIC(18, 6) NOT NULL,
    low NUMERIC(18, 6) NOT NULL,
    close NUMERIC(18, 6) NOT NULL,
    volume BIGINT NOT NULL DEFAULT 0,
    turnover NUMERIC(24, 6) NOT NULL DEFAULT 0,
    vwap NUMERIC(18, 6),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (symbol_id, timeframe, bar_time)
);

CREATE INDEX IF NOT EXISTS idx_minute_bar_symbol_time ON market.minute_bar(symbol_id, bar_time);
CREATE INDEX IF NOT EXISTS idx_minute_bar_time ON market.minute_bar(bar_time);

-- =========================
-- market.financial_indicator
-- =========================
CREATE TABLE IF NOT EXISTS market.financial_indicator (
    indicator_id BIGSERIAL PRIMARY KEY,
    symbol_id BIGINT NOT NULL REFERENCES core.symbol_info(symbol_id) ON DELETE CASCADE,
    report_date DATE NOT NULL,
    report_type VARCHAR(10) NOT NULL,
    eps NUMERIC(18, 6),
    bps NUMERIC(18, 6),
    roa NUMERIC(18, 6),
    roe NUMERIC(18, 6),
    profit_margin NUMERIC(18, 6),
    debt_to_equity NUMERIC(18, 6),
    current_ratio NUMERIC(18, 6),
    quick_ratio NUMERIC(18, 6),
    operating_cash_flow NUMERIC(24, 6),
    investing_cash_flow NUMERIC(24, 6),
    financing_cash_flow NUMERIC(24, 6),
    total_revenue NUMERIC(24, 6),
    net_profit NUMERIC(24, 6),
    total_assets NUMERIC(24, 6),
    total_liabilities NUMERIC(24, 6),
    equity NUMERIC(24, 6),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (symbol_id, report_date, report_type)
);

CREATE INDEX IF NOT EXISTS idx_financial_indicator_symbol ON market.financial_indicator(symbol_id);
CREATE INDEX IF NOT EXISTS idx_financial_indicator_report_date ON market.financial_indicator(report_date);

-- =========================
-- factor.factor_instance
-- =========================
CREATE TABLE IF NOT EXISTS factor.factor_instance (
    instance_id BIGSERIAL PRIMARY KEY,
    factor_id VARCHAR(100) NOT NULL,
    full_config JSONB NOT NULL,
    runtime_version VARCHAR(50),
    status VARCHAR(20) NOT NULL DEFAULT 'ACTIVE',
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_factor_instance_factor_id ON factor.factor_instance(factor_id);
CREATE INDEX IF NOT EXISTS idx_factor_instance_status ON factor.factor_instance(status);

-- =========================
-- factor.factor_runtime_snapshot
-- =========================
CREATE TABLE IF NOT EXISTS factor.factor_runtime_snapshot (
    snapshot_id BIGSERIAL PRIMARY KEY,
    instance_id BIGINT NOT NULL REFERENCES factor.factor_instance(instance_id) ON DELETE CASCADE,
    snapshot_type VARCHAR(50) NOT NULL,
    snapshot_payload JSONB NOT NULL,
    config_version VARCHAR(50),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_factor_runtime_snapshot_instance ON factor.factor_runtime_snapshot(instance_id);
CREATE INDEX IF NOT EXISTS idx_factor_runtime_snapshot_type ON factor.factor_runtime_snapshot(snapshot_type);

-- =========================
-- backtest.backtest_config / summary
-- =========================
CREATE TABLE IF NOT EXISTS backtest.backtest_config (
    config_id BIGSERIAL PRIMARY KEY,
    config_name VARCHAR(200) NOT NULL,
    strategy_id VARCHAR(100) NOT NULL,
    start_date DATE NOT NULL,
    end_date DATE NOT NULL,
    initial_capital NUMERIC(24, 6) NOT NULL DEFAULT 1000000,
    benchmark VARCHAR(20) NOT NULL DEFAULT '000300.SH',
    commission_rate NUMERIC(12, 8) NOT NULL DEFAULT 0.0003,
    slippage_rate NUMERIC(12, 8) NOT NULL DEFAULT 0.0001,
    parameters JSONB NOT NULL,
    description TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS backtest.backtest_summary (
    summary_id BIGSERIAL PRIMARY KEY,
    config_id BIGINT NOT NULL REFERENCES backtest.backtest_config(config_id) ON DELETE CASCADE,
    backtest_id VARCHAR(100) NOT NULL UNIQUE,
    total_return NUMERIC(18, 6) NOT NULL,
    annual_return NUMERIC(18, 6),
    sharpe_ratio NUMERIC(18, 6),
    max_drawdown NUMERIC(18, 6) NOT NULL,
    volatility NUMERIC(18, 6),
    win_rate NUMERIC(18, 6),
    profit_loss_ratio NUMERIC(18, 6),
    total_trades INTEGER NOT NULL DEFAULT 0,
    avg_holding_period INTEGER,
    alpha NUMERIC(18, 6),
    beta NUMERIC(18, 6),
    information_ratio NUMERIC(18, 6),
    benchmark_return NUMERIC(18, 6),
    final_equity NUMERIC(24, 6) NOT NULL,
    status VARCHAR(20) NOT NULL DEFAULT 'COMPLETED',
    start_time TIMESTAMPTZ NOT NULL,
    end_time TIMESTAMPTZ,
    duration_seconds INTEGER,
    log_path TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_backtest_summary_config ON backtest.backtest_summary(config_id);
CREATE INDEX IF NOT EXISTS idx_backtest_summary_status ON backtest.backtest_summary(status);

-- =========================
-- cleaning.cleaning_tasks / results
-- =========================
CREATE TABLE IF NOT EXISTS cleaning.cleaning_tasks (
    task_id BIGSERIAL PRIMARY KEY,
    task_uuid UUID NOT NULL UNIQUE,
    symbol VARCHAR(20),
    start_date DATE,
    end_date DATE,
    original_record_count BIGINT NOT NULL DEFAULT 0,
    cleaned_record_count BIGINT NOT NULL DEFAULT 0,
    removed_record_count BIGINT NOT NULL DEFAULT 0,
    data_quality_score NUMERIC(10, 4) NOT NULL DEFAULT 0,
    status VARCHAR(20) NOT NULL DEFAULT 'PENDING',
    start_time TIMESTAMPTZ,
    end_time TIMESTAMPTZ,
    duration_seconds INTEGER,
    error_message TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS cleaning.cleaning_results (
    result_id BIGSERIAL PRIMARY KEY,
    task_id BIGINT NOT NULL REFERENCES cleaning.cleaning_tasks(task_id) ON DELETE CASCADE,
    symbol VARCHAR(20) NOT NULL,
    trade_date DATE NOT NULL,
    open NUMERIC(18, 6),
    high NUMERIC(18, 6),
    low NUMERIC(18, 6),
    close NUMERIC(18, 6),
    volume BIGINT,
    turnover NUMERIC(24, 6),
    row_payload_json JSONB,
    payload_version INTEGER NOT NULL DEFAULT 1,
    is_cleaned BOOLEAN NOT NULL DEFAULT TRUE,
    cleaning_notes TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (task_id, symbol, trade_date)
);

CREATE INDEX IF NOT EXISTS idx_cleaning_results_symbol_date ON cleaning.cleaning_results(symbol, trade_date);
CREATE INDEX IF NOT EXISTS idx_cleaning_results_is_cleaned ON cleaning.cleaning_results(is_cleaned);
