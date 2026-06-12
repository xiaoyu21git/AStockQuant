#pragma once

#include "DatabaseConfig.h"
#include "ISqlDatabase.h"

#include <memory>

namespace astock {
namespace database {

class NativeMySQLDatabase final : public ISqlDatabase {
public:
    explicit NativeMySQLDatabase(const DatabaseConfig& config);
    ~NativeMySQLDatabase() override;

    NativeMySQLDatabase(const NativeMySQLDatabase&) = delete;
    NativeMySQLDatabase& operator=(const NativeMySQLDatabase&) = delete;
    NativeMySQLDatabase(NativeMySQLDatabase&& other) noexcept;
    NativeMySQLDatabase& operator=(NativeMySQLDatabase&& other) noexcept;

    bool open() override;
    void close() override;
    [[nodiscard]] bool isOpen() const override;

    [[nodiscard]] SqlQueryResult executeQuery(const std::string& sql,
                                               const std::vector<SqlParam>& params = {}) override;
    int executeUpdate(const std::string& sql,
                      const std::vector<SqlParam>& params = {}) override;

    int executeBatchUpdate(const std::string& sql,
                           const std::vector<std::vector<SqlParam>>& batchParams) override;

    bool beginTransaction() override;
    bool commitTransaction() override;
    bool rollbackTransaction() override;

    [[nodiscard]] std::string lastError() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace database
} // namespace astock