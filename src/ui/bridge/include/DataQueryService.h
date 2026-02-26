// DataQueryService.h
// 数据查询服务 - 无Qt依赖，纯C++数据库查询服务
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <stdexcept>
#include <chrono>

// 前向声明
class DatabaseConnectionService;
namespace astock::database {
    class QueryBuilder;
}

namespace foundation::thread {
    class IExecutor;
}

// 数据频率类型
enum class DataFrequency {
    DAILY,
    WEEKLY,
    MINUTE_1,
    MINUTE_5,
    MINUTE_15,
    MINUTE_30,
    MINUTE_60
};

// 查询结果行（使用标准库类型）
struct QueryRow {
    std::map<std::string, std::string> fields;
    
    const std::string& getString(const std::string& key, const std::string& defaultValue = "") const {
        auto it = fields.find(key);
        return (it != fields.end()) ? it->second : defaultValue;
    }
    
    double getDouble(const std::string& key, double defaultValue = 0.0) const;
    int getInt(const std::string& key, int defaultValue = 0) const;
    long long getLongLong(const std::string& key, long long defaultValue = 0) const;
    
    bool hasField(const std::string& key) const {
        return fields.find(key) != fields.end();
    }
};

// 查询结果
struct QueryResult {
    std::vector<QueryRow> rows;
    size_t rowCount{0};
    size_t affectedRows{0};
    std::chrono::milliseconds executionTime{0};
    std::string errorMessage;
    bool success{true};
    
    bool isEmpty() const { return rows.empty(); }
    size_t size() const { return rows.size(); }
    const QueryRow* getRow(size_t index) const {
        return (index < rows.size()) ? &rows[index] : nullptr;
    }
};

// 查询参数
struct QueryParams {
    std::string symbol;
    std::string startDate;
    std::string endDate;
    DataFrequency frequency{DataFrequency::DAILY};
    int limit{0};
    int offset{0};
    std::vector<std::string> columns;
    std::map<std::string, std::string> extraParams;
};

// 查询事件类型
enum class QueryEventType {
    QUERY_STARTED,
    QUERY_COMPLETED,
    QUERY_FAILED,
    QUERY_CANCELLED
};

// 查询事件回调
using QueryEventCallback = std::function<void(
    QueryEventType eventType,
    const std::string& queryId,
    const std::string& message,
    const QueryResult* result = nullptr
)>;

// 数据查询服务 - 纯C++，无Qt依赖
class DataQueryService {
public:
    // 禁止拷贝和赋值
    DataQueryService(const DataQueryService&) = delete;
    DataQueryService& operator=(const DataQueryService&) = delete;
    
    // 构造和析构
    explicit DataQueryService(std::shared_ptr<DatabaseConnectionService> connectionService);
    ~DataQueryService();
    
    // 同步查询方法
    QueryResult queryDailyData(const std::string& symbol, 
                              const std::string& startDate, 
                              const std::string& endDate);
    
    QueryResult queryWeeklyData(const std::string& symbol, 
                               const std::string& startDate, 
                               const std::string& endDate);
    
    QueryResult queryMinuteData(const std::string& symbol,
                               DataFrequency frequency,
                               const std::chrono::system_clock::time_point& startTime,
                               const std::chrono::system_clock::time_point& endTime);
    
    QueryResult queryData(const QueryParams& params);
    
    // 异步查询方法
    std::string queryDailyDataAsync(const std::string& symbol,
                                   const std::string& startDate,
                                   const std::string& endDate,
                                   std::function<void(const QueryResult&)> callback = nullptr);
    
    std::string queryDataAsync(const QueryParams& params,
                              std::function<void(const QueryResult&)> callback = nullptr);
    
    // 批量查询
    std::vector<QueryResult> batchQueryDailyData(const std::vector<std::string>& symbols,
                                                const std::string& startDate,
                                                const std::string& endDate);
    
    std::string batchQueryDailyDataAsync(const std::vector<std::string>& symbols,
                                        const std::string& startDate,
                                        const std::string& endDate,
                                        std::function<void(const std::vector<QueryResult>&)> callback = nullptr);
    
    // 查询控制
    bool cancelQuery(const std::string& queryId);
    void cancelAllQueries();
    
    // 查询状态
    struct QueryStatus {
        std::string queryId;
        bool isRunning{false};
        bool isCompleted{false};
        bool isCancelled{false};
        std::chrono::system_clock::time_point startTime;
        std::chrono::system_clock::time_point endTime;
        size_t progress{0}; // 0-100
        std::string currentSymbol; // 对于批量查询
    };
    
    QueryStatus getQueryStatus(const std::string& queryId) const;
    std::vector<QueryStatus> getAllQueryStatus() const;
    
    // 事件监听
    void addQueryEventListener(QueryEventCallback callback);
    void removeQueryEventListeners();
    
    // 统计信息
    struct Statistics {
        size_t totalQueries{0};
        size_t successfulQueries{0};
        size_t failedQueries{0};
        size_t cancelledQueries{0};
        size_t totalRowsReturned{0};
        std::chrono::milliseconds totalExecutionTime{0};
        std::chrono::milliseconds avgExecutionTime{0};
        std::chrono::milliseconds maxExecutionTime{0};
        std::chrono::milliseconds minExecutionTime{0};
        
        std::string toString() const;
    };
    
    Statistics getStatistics() const;
    void resetStatistics();
    
    // 缓存管理（可选）
    void enableCache(bool enable);
    void clearCache();
    size_t getCacheSize() const;
    
    // 工具方法
    static std::string frequencyToString(DataFrequency frequency);
    static DataFrequency stringToFrequency(const std::string& freqStr);
    
    static std::string dateToString(const std::chrono::system_clock::time_point& timePoint);
    static std::chrono::system_clock::time_point stringToDate(const std::string& dateStr);
    
private:
    // 内部实现
    class Impl;
    std::unique_ptr<Impl> m_impl;
    
    // 事件通知
    void notifyQueryEvent(QueryEventType type,
                         const std::string& queryId,
                         const std::string& message,
                         const QueryResult* result = nullptr);
    
    // 线程池执行
    void executeAsync(std::function<void()> task);
    
    // 查询ID生成
    std::string generateQueryId() const;
};

// 异常类
class DataQueryException : public std::runtime_error {
public:
    explicit DataQueryException(const std::string& message,
                               const std::string& queryId = "",
                               bool isConnectionError = false)
        : std::runtime_error(message), m_queryId(queryId), m_isConnectionError(isConnectionError) {}
    
    const std::string& getQueryId() const { return m_queryId; }
    bool isConnectionError() const { return m_isConnectionError; }
    
private:
    std::string m_queryId;
    bool m_isConnectionError;
};

// 工厂函数
std::shared_ptr<DataQueryService> createDataQueryService(
    std::shared_ptr<DatabaseConnectionService> connectionService,
    std::shared_ptr<foundation::thread::IExecutor> executor = nullptr
);