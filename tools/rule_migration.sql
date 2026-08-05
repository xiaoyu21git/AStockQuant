-- 规则系统运行时配置表 (v0.15.0)
-- 执行: psql -h <host> -U <user> -d <db> -f tools/rule_migration.sql

-- 规则启用/禁用/降级控制
CREATE TABLE IF NOT EXISTS live.rule_state (
    rule_id       VARCHAR(64) PRIMARY KEY,
    template_id   VARCHAR(128) NOT NULL,
    enabled       BOOLEAN NOT NULL DEFAULT TRUE,
    severity      VARCHAR(16) NOT NULL DEFAULT 'active'
                  CHECK (severity IN ('active', 'degraded', 'disabled')),
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS idx_rule_state_template ON live.rule_state(template_id);

-- 加密补丁注册表 (防重复加载 + 回滚检测)
CREATE TABLE IF NOT EXISTS live.rule_patch_registry (
    patch_id      VARCHAR(64) PRIMARY KEY,
    patch_hash    VARCHAR(128) NOT NULL,
    patch_version INTEGER NOT NULL DEFAULT 1,
    depends_on    INTEGER,  -- 依赖的前置补丁版本(可选)
    applied_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    description   TEXT
);
CREATE INDEX IF NOT EXISTS idx_patch_version ON live.rule_patch_registry(patch_version);
