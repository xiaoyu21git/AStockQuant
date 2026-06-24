#include "../include/database/NativePgDatabase.h"

#include <libpq-fe.h>

#include <cstring>
#include <sstream>
#include <stdexcept>

// PG type OIDs (from catalog/pg_type.h, not available in libpq client headers)
static constexpr Oid PG_INT4   = 23;
static constexpr Oid PG_INT8   = 20;
static constexpr Oid PG_FLOAT8 = 701;
static constexpr Oid PG_TEXT   = 25;

namespace astock {
namespace database {

namespace {

// 将 MySQL 风格 ? 占位符转为 PG $1, $2, ...
// 并构造 params 数组给 PQexecParams
std::string convertPlaceholders(const std::string& sql, int& outCount) {
    outCount = 0;
    bool inString = false;
    for (char c : sql) {
        if (c == '\'') inString = !inString;
        if (c == '?' && !inString) ++outCount;
    }

    std::string result;
    result.reserve(sql.size() + outCount * 3);
    int idx = 0;
    inString = false;
    for (char c : sql) {
        if (c == '\'') inString = !inString;
        if (c == '?' && !inString) {
            result += '$';
            result += std::to_string(++idx);
        } else {
            result += c;
        }
    }
    return result;
}

// PG 参数值 + OID
struct PgParam {
    std::string value;
    Oid oid;
};

std::vector<PgParam> buildParams(const std::vector<SqlParam>& params) {
    std::vector<PgParam> result;
    result.reserve(params.size());
    for (const auto& p : params) {
        if (std::holds_alternative<std::int32_t>(p)) {
            result.push_back({std::to_string(std::get<std::int32_t>(p)), PG_INT4});
        } else if (std::holds_alternative<std::int64_t>(p)) {
            result.push_back({std::to_string(std::get<std::int64_t>(p)), PG_INT8});
        } else if (std::holds_alternative<double>(p)) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.15g", std::get<double>(p));
            result.push_back({buf, PG_FLOAT8});
        } else if (std::holds_alternative<std::string>(p)) {
            result.push_back({std::get<std::string>(p), PG_TEXT});
        }
    }
    return result;
}

SqlQueryResult buildResult(PGresult* res) {
    SqlQueryResult result;
    int rows = PQntuples(res);
    int cols = PQnfields(res);
    for (int r = 0; r < rows; ++r) {
        SqlQueryResultRow row;
        for (int c = 0; c < cols; ++c) {
            const char* val = PQgetvalue(res, r, c);
            row.setValue(PQfname(res, c), val ? val : "");
        }
        result.addRow(row);
    }
    return result;
}

} // namespace

struct NativePgDatabase::Impl {
    PGconn* conn = nullptr;
    std::string lastError_;
    bool open_ = false;

    ~Impl() {
        if (conn) PQfinish(conn);
    }
};

NativePgDatabase::NativePgDatabase(const DatabaseConfig& config)
    : impl_(std::make_unique<Impl>())
{
    std::ostringstream connStr;
    connStr << "host=" << config.host
            << " port=" << config.port
            << " dbname=" << config.database
            << " user=" << config.username
            << " password=" << config.password;
    if (!config.charset.empty())
        connStr << " options='-c client_encoding=" << config.charset << "'";

    impl_->conn = PQconnectdb(connStr.str().c_str());
    if (PQstatus(impl_->conn) != CONNECTION_OK) {
        impl_->lastError_ = PQerrorMessage(impl_->conn);
        PQfinish(impl_->conn);
        impl_->conn = nullptr;
        throw std::runtime_error("PQconnectdb failed: " + impl_->lastError_);
    }
    // 全局 search_path：无 schema 前缀的 SQL 自动搜索所有业务 schema
    PGresult* sp = PQexec(impl_->conn, "SET search_path TO ref, mkt, fund, alpha, live, port, data, public");
    PQclear(sp);
    impl_->open_ = true;
}

NativePgDatabase::~NativePgDatabase() = default;

bool NativePgDatabase::open() {
    return impl_->open_;
}

void NativePgDatabase::close() {
    if (impl_->conn) {
        PQfinish(impl_->conn);
        impl_->conn = nullptr;
    }
    impl_->open_ = false;
}

bool NativePgDatabase::isOpen() const {
    if (!impl_->conn) return false;
    return PQstatus(impl_->conn) == CONNECTION_OK;
}

SqlQueryResult NativePgDatabase::executeQuery(const std::string& sql,
                                               const std::vector<SqlParam>& params) {
    if (!impl_->conn || !impl_->open_) {
        SqlQueryResult empty;
        return empty;
    }

    int paramCount = 0;
    std::string pgSql = convertPlaceholders(sql, paramCount);

    if (params.empty() || params.size() != static_cast<size_t>(paramCount)) {
        // 无参数或参数数量不匹配：直接执行
        PGresult* res = PQexec(impl_->conn, pgSql.c_str());
        if (PQresultStatus(res) != PGRES_TUPLES_OK && PQresultStatus(res) != PGRES_COMMAND_OK) {
            impl_->lastError_ = PQresultErrorMessage(res);
            PQclear(res);
            return {};
        }
        auto result = buildResult(res);
        PQclear(res);
        return result;
    }

    // 参数化查询
    auto pgParams = buildParams(params);
    std::vector<const char*> paramValues;
    std::vector<Oid> paramOids;
    paramValues.reserve(pgParams.size());
    paramOids.reserve(pgParams.size());
    for (auto& p : pgParams) {
        paramValues.push_back(p.value.c_str());
        paramOids.push_back(p.oid);
    }

    PGresult* res = PQexecParams(impl_->conn, pgSql.c_str(),
                                  static_cast<int>(pgParams.size()),
                                  paramOids.data(),
                                  paramValues.data(),
                                  nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK && PQresultStatus(res) != PGRES_COMMAND_OK) {
        impl_->lastError_ = PQresultErrorMessage(res);
        PQclear(res);
        return {};
    }
    auto result = buildResult(res);
    PQclear(res);
    return result;
}

int NativePgDatabase::executeUpdate(const std::string& sql,
                                     const std::vector<SqlParam>& params) {
    if (!impl_->conn || !impl_->open_) return 0;

    int paramCount = 0;
    std::string pgSql = convertPlaceholders(sql, paramCount);

    if (params.empty()) {
        PGresult* res = PQexec(impl_->conn, pgSql.c_str());
        if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) {
            impl_->lastError_ = PQresultErrorMessage(res);
            PQclear(res);
            return 0;
        }
        int affected = std::atoi(PQcmdTuples(res));
        PQclear(res);
        return affected;
    }

    auto pgParams = buildParams(params);
    std::vector<const char*> paramValues;
    std::vector<Oid> paramOids;
    paramValues.reserve(pgParams.size());
    paramOids.reserve(pgParams.size());
    for (auto& p : pgParams) {
        paramValues.push_back(p.value.c_str());
        paramOids.push_back(p.oid);
    }

    PGresult* res = PQexecParams(impl_->conn, pgSql.c_str(),
                                  static_cast<int>(pgParams.size()),
                                  paramOids.data(),
                                  paramValues.data(),
                                  nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) {
        impl_->lastError_ = PQresultErrorMessage(res);
        PQclear(res);
        return 0;
    }
    int affected = std::atoi(PQcmdTuples(res));
    PQclear(res);
    return affected;
}

int NativePgDatabase::executeBatchUpdate(const std::string& sql,
                                          const std::vector<std::vector<SqlParam>>& batchParams) {
    int total = 0;
    for (const auto& params : batchParams) {
        total += executeUpdate(sql, params);
    }
    return total;
}

bool NativePgDatabase::beginTransaction() {
    return executeUpdate("BEGIN") >= 0;
}

bool NativePgDatabase::commitTransaction() {
    return executeUpdate("COMMIT") >= 0;
}

bool NativePgDatabase::rollbackTransaction() {
    return executeUpdate("ROLLBACK") >= 0;
}

std::string NativePgDatabase::lastError() const {
    return impl_->lastError_;
}

} // namespace database
} // namespace astock
