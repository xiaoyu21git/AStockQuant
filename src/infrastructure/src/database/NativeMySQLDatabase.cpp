#include "../include/database/NativeMySQLDatabase.h"

#include <mysql.h>

#include <cstring>
#include <sstream>

namespace astock {
namespace database {

namespace {

std::string bindParams(const std::string& sql, const std::vector<SqlParam>& params)
{
    if (params.empty()) {
        return sql;
    }

    std::string result;
    result.reserve(sql.size() + params.size() * 16);

    std::size_t paramIndex = 0;
    for (std::size_t i = 0; i < sql.size(); ++i) {
        if (sql[i] == '?' && paramIndex < params.size()) {
            const SqlParam& param = params[paramIndex++];
            if (std::holds_alternative<std::int32_t>(param)) {
                result += std::to_string(std::get<std::int32_t>(param));
            } else if (std::holds_alternative<std::int64_t>(param)) {
                result += std::to_string(std::get<std::int64_t>(param));
            } else if (std::holds_alternative<double>(param)) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.10g", std::get<double>(param));
                result += buf;
            } else if (std::holds_alternative<std::string>(param)) {
                result += '\'';
                result += std::get<std::string>(param);
                result += '\'';
            }
        } else {
            result += sql[i];
        }
    }
    return result;
}

} // namespace

struct NativeMySQLDatabase::Impl {
    MYSQL* conn = nullptr;
    std::string lastError_;
    bool open_ = false;

    ~Impl()
    {
        if (conn) {
            mysql_close(conn);
        }
    }
};

NativeMySQLDatabase::NativeMySQLDatabase(const DatabaseConfig& config)
    : impl_(std::make_unique<Impl>())
{
    impl_->conn = mysql_init(nullptr);
    if (!impl_->conn) {
        throw std::runtime_error("mysql_init failed");
    }

    unsigned long clientFlag = CLIENT_MULTI_STATEMENTS | CLIENT_MULTI_RESULTS;

    unsigned int connectTimeout = static_cast<unsigned int>(config.connect_timeout.count());
    mysql_options(impl_->conn, MYSQL_OPT_CONNECT_TIMEOUT, &connectTimeout);

    unsigned int readTimeout = static_cast<unsigned int>(config.read_timeout.count());
    mysql_options(impl_->conn, MYSQL_OPT_READ_TIMEOUT, &readTimeout);

    unsigned int writeTimeout = static_cast<unsigned int>(config.write_timeout.count());
    mysql_options(impl_->conn, MYSQL_OPT_WRITE_TIMEOUT, &writeTimeout);

    mysql_options(impl_->conn, MYSQL_SET_CHARSET_NAME, config.charset.c_str());

    if (!config.host.empty()) {
        MYSQL* raw = mysql_real_connect(impl_->conn, config.host.c_str(), config.username.c_str(),
                                        config.password.c_str(), config.database.c_str(), config.port,
                                        nullptr, clientFlag);
        if (!raw) {
            std::string err = mysql_error(impl_->conn);
            throw std::runtime_error("mysql_real_connect failed: " + err);
        }
        impl_->open_ = true;
    }
}

NativeMySQLDatabase::~NativeMySQLDatabase() = default;

NativeMySQLDatabase::NativeMySQLDatabase(NativeMySQLDatabase&& other) noexcept
    : impl_(std::move(other.impl_))
{
}

NativeMySQLDatabase& NativeMySQLDatabase::operator=(NativeMySQLDatabase&& other) noexcept
{
    if (this != &other) {
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool NativeMySQLDatabase::open()
{
    return impl_->open_;
}

void NativeMySQLDatabase::close()
{
    if (impl_->conn) {
        mysql_close(impl_->conn);
        impl_->conn = nullptr;
        impl_->open_ = false;
    }
}

bool NativeMySQLDatabase::isOpen() const
{
    if (!impl_->conn || !impl_->open_) {
        return false;
    }
    return mysql_ping(impl_->conn) == 0;
}

SqlQueryResult NativeMySQLDatabase::executeQuery(const std::string& sql,
                                                  const std::vector<SqlParam>& params)
{
    SqlQueryResult result;
    if (!impl_->conn || !impl_->open_) {
        impl_->lastError_ = "Connection not open";
        return result;
    }

    const std::string boundSql = bindParams(sql, params);
    if (mysql_query(impl_->conn, boundSql.c_str()) != 0) {
        impl_->lastError_ = mysql_error(impl_->conn);
        return result;
    }

    MYSQL_RES* mysqlResult = mysql_store_result(impl_->conn);
    if (!mysqlResult) {
        if (mysql_field_count(impl_->conn) == 0) {
            return result; // not a SELECT
        }
        impl_->lastError_ = mysql_error(impl_->conn);
        return result;
    }

    const unsigned int numFields = mysql_num_fields(mysqlResult);
    std::vector<std::string> fieldNames(numFields);
    MYSQL_FIELD* fields = mysql_fetch_fields(mysqlResult);
    for (unsigned int i = 0; i < numFields; ++i) {
        fieldNames[i] = fields[i].name;
    }

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(mysqlResult))) {
        unsigned long* lengths = mysql_fetch_lengths(mysqlResult);
        SqlQueryResultRow resultRow;
        for (unsigned int i = 0; i < numFields; ++i) {
            std::string value = row[i] ? std::string(row[i], lengths[i]) : std::string();
            resultRow.setValue(fieldNames[i], value);
        }
        result.addRow(resultRow);
    }

    mysql_free_result(mysqlResult);

    while (mysql_next_result(impl_->conn) == 0) {
        MYSQL_RES* extra = mysql_store_result(impl_->conn);
        if (extra) {
            mysql_free_result(extra);
        }
    }

    return result;
}

int NativeMySQLDatabase::executeUpdate(const std::string& sql, const std::vector<SqlParam>& params)
{
    if (!impl_->conn || !impl_->open_) {
        impl_->lastError_ = "Connection not open";
        return -1;
    }

    const std::string boundSql = bindParams(sql, params);
    if (mysql_query(impl_->conn, boundSql.c_str()) != 0) {
        impl_->lastError_ = mysql_error(impl_->conn);
        return -1;
    }

    int affectedRows = static_cast<int>(mysql_affected_rows(impl_->conn));

    MYSQL_RES* mysqlResult = mysql_store_result(impl_->conn);
    if (mysqlResult) {
        mysql_free_result(mysqlResult);
    }
    while (mysql_next_result(impl_->conn) == 0) {
        MYSQL_RES* extra = mysql_store_result(impl_->conn);
        if (extra) {
            mysql_free_result(extra);
        }
    }

    return affectedRows;
}

bool NativeMySQLDatabase::beginTransaction()
{
    return executeUpdate("START TRANSACTION") >= 0;
}

bool NativeMySQLDatabase::commitTransaction()
{
    return executeUpdate("COMMIT") >= 0;
}

bool NativeMySQLDatabase::rollbackTransaction()
{
    return executeUpdate("ROLLBACK") >= 0;
}

std::string NativeMySQLDatabase::lastError() const
{
    return impl_->lastError_;
}

} // namespace database
} // namespace astock