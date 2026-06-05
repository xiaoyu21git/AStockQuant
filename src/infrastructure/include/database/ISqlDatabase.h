#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace astock {
namespace database {

using SqlParam = std::variant<std::int32_t, std::int64_t, double, std::string>;

class SqlQueryResultRow {
public:
    void setValue(const std::string& column, const std::string& value)
    {
        data_[column] = value;
    }

    [[nodiscard]] std::string getString(const std::string& column, const std::string& defaultValue = "") const
    {
        auto it = data_.find(column);
        return it != data_.end() ? it->second : defaultValue;
    }

    [[nodiscard]] int getInt(const std::string& column, int defaultValue = 0) const
    {
        auto it = data_.find(column);
        if (it != data_.end()) {
            try {
                return std::stoi(it->second);
            } catch (...) {
            }
        }
        return defaultValue;
    }

    [[nodiscard]] double getDouble(const std::string& column, double defaultValue = 0.0) const
    {
        auto it = data_.find(column);
        if (it != data_.end()) {
            try {
                return std::stod(it->second);
            } catch (...) {
            }
        }
        return defaultValue;
    }

    [[nodiscard]] bool contains(const std::string& column) const
    {
        return data_.find(column) != data_.end();
    }

    [[nodiscard]] const std::map<std::string, std::string>& getValues() const
    {
        return data_;
    }

private:
    std::map<std::string, std::string> data_;
};

class SqlQueryResult {
public:
    void addRow(const SqlQueryResultRow& row)
    {
        rows_.push_back(row);
    }

    [[nodiscard]] std::size_t rowCount() const
    {
        return rows_.size();
    }

    [[nodiscard]] bool isEmpty() const
    {
        return rows_.empty();
    }

    [[nodiscard]] const SqlQueryResultRow& getRow(std::size_t index) const
    {
        return rows_.at(index);
    }

    [[nodiscard]] const std::vector<SqlQueryResultRow>& getRows() const
    {
        return rows_;
    }

private:
    std::vector<SqlQueryResultRow> rows_;
};

/// 通用 SQL 数据库抽象接口，不绑定特定数据库类型
class ISqlDatabase {
public:
    virtual ~ISqlDatabase() = default;

    virtual bool open() = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;

    [[nodiscard]] virtual SqlQueryResult executeQuery(const std::string& sql,
                                                       const std::vector<SqlParam>& params = {}) = 0;
    virtual int executeUpdate(const std::string& sql,
                              const std::vector<SqlParam>& params = {}) = 0;

    virtual bool beginTransaction() = 0;
    virtual bool commitTransaction() = 0;
    virtual bool rollbackTransaction() = 0;

    [[nodiscard]] virtual std::string lastError() const = 0;
};

} // namespace database
} // namespace astock