#pragma once

#include "../../../infrastructure/include/database/ISqlDatabase.h"
#include "../../../infrastructure/include/database/QtMySQLDatabase.h"

namespace astock {
namespace database {

class QtSqlDatabaseAdapter final : public ISqlDatabase {
public:
    explicit QtSqlDatabaseAdapter(std::shared_ptr<QtMySQLDatabase> db)
        : db_(std::move(db))
    {
    }

    bool open() override
    {
        return db_ ? db_->open() : false;
    }

    void close() override
    {
        if (db_) db_->close();
    }

    [[nodiscard]] bool isOpen() const override
    {
        return db_ ? db_->isOpen() : false;
    }

    [[nodiscard]] SqlQueryResult executeQuery(const std::string& sql,
                                               const std::vector<SqlParam>& params) override
    {
        SqlQueryResult result;
        if (!db_) return result;

        std::map<QString, QVariant> qtParams;
        int index = 0;
        for (const auto& param : params) {
            QString key = QStringLiteral("__param_%1").arg(index++, 8, 10, QLatin1Char('0'));
            if (std::holds_alternative<std::int32_t>(param)) {
                qtParams[key] = QVariant::fromValue(std::get<std::int32_t>(param));
            } else if (std::holds_alternative<std::int64_t>(param)) {
                qtParams[key] = QVariant::fromValue(static_cast<qlonglong>(std::get<std::int64_t>(param)));
            } else if (std::holds_alternative<double>(param)) {
                qtParams[key] = QVariant::fromValue(std::get<double>(param));
            } else if (std::holds_alternative<std::string>(param)) {
                qtParams[key] = QVariant::fromValue(QString::fromStdString(std::get<std::string>(param)));
            }
        }

        QueryResult qtResult = db_->executeQuery(QString::fromStdString(sql), qtParams);
        for (size_t i = 0; i < qtResult.rowCount(); ++i) {
            SqlQueryResultRow nativeRow;
            const auto& qtRow = qtResult.getRow(i);
            for (const auto& [key, value] : qtRow.getValues()) {
                nativeRow.setValue(key.toStdString(), value.toString().toStdString());
            }
            result.addRow(nativeRow);
        }
        return result;
    }

    int executeUpdate(const std::string& sql, const std::vector<SqlParam>& params) override
    {
        if (!db_) return -1;

        std::map<QString, QVariant> qtParams;
        int index = 0;
        for (const auto& param : params) {
            QString key = QStringLiteral("__param_%1").arg(index++, 8, 10, QLatin1Char('0'));
            if (std::holds_alternative<std::int32_t>(param)) {
                qtParams[key] = QVariant::fromValue(std::get<std::int32_t>(param));
            } else if (std::holds_alternative<std::int64_t>(param)) {
                qtParams[key] = QVariant::fromValue(static_cast<qlonglong>(std::get<std::int64_t>(param)));
            } else if (std::holds_alternative<double>(param)) {
                qtParams[key] = QVariant::fromValue(std::get<double>(param));
            } else if (std::holds_alternative<std::string>(param)) {
                qtParams[key] = QVariant::fromValue(QString::fromStdString(std::get<std::string>(param)));
            }
        }

        return db_->executeUpdate(QString::fromStdString(sql), qtParams);
    }

    bool beginTransaction() override { return db_ ? db_->beginTransaction() != nullptr : false; }
    bool commitTransaction() override { return db_ ? db_->commitTransaction() : false; }
    bool rollbackTransaction() override { return db_ ? db_->rollbackTransaction() : false; }

    [[nodiscard]] std::string lastError() const override
    {
        return db_ ? db_->getLastError().toStdString() : std::string();
    }

private:
    std::shared_ptr<QtMySQLDatabase> db_;
};

} // namespace database
} // namespace astock