#include "DataCleaningPersistence.h"

#include <QDebug>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>

#include "../../infrastructure/include/database/ConnectionPool.h"

namespace ui::bridge {

DataCleaningPersistence::DataCleaningPersistence(QObject* parent)
    : QObject(parent)
{
    qDebug() << "DataCleaningPersistence: Created";
}

DataCleaningPersistence::~DataCleaningPersistence()
{
    qDebug() << "DataCleaningPersistence: Destroyed";
}

bool DataCleaningPersistence::saveCleaningResult(const QString& taskId, 
                                                const QVariantList& cleanedData,
                                                const QVariantMap& stats)
{
    try {
        qDebug() << "DataCleaningPersistence: Saving cleaning result for task" << taskId;
        
        // 获取数据库连接
        auto connection = astock::database::ConnectionPool::instance().getConnection();
        if (!connection.isValid()) {
            qWarning() << "DataCleaningPersistence: Invalid database connection";
            return false;
        }
        
        // 开始事务
        if (!connection.transaction()) {
            qWarning() << "DataCleaningPersistence: Failed to start transaction";
            return false;
        }
        
        // 1. 保存清洗任务记录
        if (!saveCleaningTask(connection, taskId, stats)) {
            connection.rollback();
            return false;
        }
        
        // 2. 保存清洗结果数据
        if (!saveCleanedData(connection, taskId, cleanedData)) {
            connection.rollback();
            return false;
        }
        
        // 提交事务
        if (!connection.commit()) {
            qWarning() << "DataCleaningPersistence: Failed to commit transaction";
            connection.rollback();
            return false;
        }
        
        qDebug() << "DataCleaningPersistence: Successfully saved cleaning result for task" << taskId;
        emit dataSaved(taskId);
        return true;
        
    } catch (const std::exception& e) {
        qCritical() << "DataCleaningPersistence: Error saving cleaning result:" << e.what();
        return false;
    }
}

bool DataCleaningPersistence::saveCleaningTask(QSqlDatabase& connection, 
                                              const QString& taskId,
                                              const QVariantMap& stats)
{
    try {
        QSqlQuery query(connection);
        
        // 检查任务是否已存在
        query.prepare("SELECT COUNT(*) FROM cleaning_tasks WHERE task_uuid = :task_uuid");
        query.bindValue(":task_uuid", taskId);
        
        if (!query.exec()) {
            qWarning() << "DataCleaningPersistence: Failed to check existing task:" 
                       << query.lastError().text();
            return false;
        }
        
        bool taskExists = false;
        if (query.next()) {
            taskExists = query.value(0).toInt() > 0;
        }
        
        if (taskExists) {
            // 更新现有任务
            query.prepare(
                "UPDATE cleaning_tasks SET "
                "original_record_count = :original_count, "
                "cleaned_record_count = :cleaned_count, "
                "removed_record_count = :removed_count, "
                "data_quality_score = :quality_score, "
                "status = :status, "
                "end_time = :end_time, "
                "duration_seconds = :duration "
                "WHERE task_uuid = :task_uuid"
            );
        } else {
            // 插入新任务
            query.prepare(
                "INSERT INTO cleaning_tasks ("
                "task_uuid, symbol, start_date, end_date, "
                "original_record_count, cleaned_record_count, removed_record_count, "
                "data_quality_score, status, start_time, end_time, duration_seconds"
                ") VALUES ("
                ":task_uuid, :symbol, :start_date, :end_date, "
                ":original_count, :cleaned_count, :removed_count, "
                ":quality_score, :status, :start_time, :end_time, :duration"
                ")"
            );
            
            // 设置默认值
            query.bindValue(":symbol", "UNKNOWN");
            query.bindValue(":start_date", QDate::currentDate().addDays(-30));
            query.bindValue(":end_date", QDate::currentDate());
            query.bindValue(":start_time", QDateTime::currentDateTime().addSecs(-60));
        }
        
        // 绑定参数
        query.bindValue(":task_uuid", taskId);
        query.bindValue(":original_count", stats.value("totalRecords", 0).toInt());
        query.bindValue(":cleaned_count", stats.value("cleanedRecords", 0).toInt());
        query.bindValue(":removed_count", stats.value("removedRecords", 0).toInt());
        query.bindValue(":quality_score", calculateQualityScore(stats));
        query.bindValue(":status", "COMPLETED");
        query.bindValue(":end_time", QDateTime::currentDateTime());
        query.bindValue(":duration", stats.value("durationMs", 0).toInt() / 1000);
        
        if (!query.exec()) {
            qWarning() << "DataCleaningPersistence: Failed to save cleaning task:" 
                       << query.lastError().text();
            return false;
        }
        
        qDebug() << "DataCleaningPersistence: Saved cleaning task" << taskId;
        return true;
        
    } catch (const std::exception& e) {
        qWarning() << "DataCleaningPersistence: Error saving cleaning task:" << e.what();
        return false;
    }
}

bool DataCleaningPersistence::saveCleanedData(QSqlDatabase& connection,
                                             const QString& taskId,
                                             const QVariantList& cleanedData)
{
    try {
        if (cleanedData.isEmpty()) {
            qDebug() << "DataCleaningPersistence: No cleaned data to save";
            return true;
        }
        
        // 获取任务ID
        int taskIdInt = getTaskId(connection, taskId);
        if (taskIdInt <= 0) {
            qWarning() << "DataCleaningPersistence: Invalid task ID for" << taskId;
            return false;
        }
        
        // 批量插入数据
        QSqlQuery query(connection);
        query.prepare(
            "INSERT INTO cleaning_results ("
            "task_id, symbol, trade_date, open, high, low, close, volume, turnover, is_cleaned"
            ") VALUES ("
            ":task_id, :symbol, :trade_date, :open, :high, :low, :close, :volume, :turnover, 1"
            ")"
        );
        
        int batchSize = 0;
        const int maxBatchSize = 1000;
        
        for (const QVariant& item : cleanedData) {
            if (!item.canConvert<QVariantMap>()) {
                continue;
            }
            
            QVariantMap record = item.toMap();
            
            query.bindValue(":task_id", taskIdInt);
            query.bindValue(":symbol", record.value("symbol", ""));
            query.bindValue(":trade_date", record.value("date", ""));
            query.bindValue(":open", record.value("open", 0.0));
            query.bindValue(":high", record.value("high", 0.0));
            query.bindValue(":low", record.value("low", 0.0));
            query.bindValue(":close", record.value("close", 0.0));
            query.bindValue(":volume", record.value("volume", 0));
            query.bindValue(":turnover", record.value("turnover", 0.0));
            
            if (!query.exec()) {
                qWarning() << "DataCleaningPersistence: Failed to insert cleaned data:" 
                           << query.lastError().text();
                return false;
            }
            
            batchSize++;
            
            // 每插入一定数量后提交
            if (batchSize >= maxBatchSize) {
                // 可以在这里添加批量提交逻辑
                batchSize = 0;
            }
        }
        
        qDebug() << "DataCleaningPersistence: Saved" << cleanedData.size() << "cleaned records";
        return true;
        
    } catch (const std::exception& e) {
        qWarning() << "DataCleaningPersistence: Error saving cleaned data:" << e.what();
        return false;
    }
}

int DataCleaningPersistence::getTaskId(QSqlDatabase& connection, const QString& taskUuid)
{
    try {
        QSqlQuery query(connection);
        query.prepare("SELECT task_id FROM cleaning_tasks WHERE task_uuid = :task_uuid");
        query.bindValue(":task_uuid", taskUuid);
        
        if (!query.exec()) {
            qWarning() << "DataCleaningPersistence: Failed to get task ID:" 
                       << query.lastError().text();
            return -1;
        }
        
        if (query.next()) {
            return query.value(0).toInt();
        }
        
        return -1;
        
    } catch (const std::exception& e) {
        qWarning() << "DataCleaningPersistence: Error getting task ID:" << e.what();
        return -1;
    }
}

double DataCleaningPersistence::calculateQualityScore(const QVariantMap& stats)
{
    int total = stats.value("totalRecords", 0).toInt();
    int cleaned = stats.value("cleanedRecords", 0).toInt();
    
    if (total <= 0) {
        return 0.0;
    }
    
    // 简单的质量评分：清洗后数据占比
    double score = (cleaned * 100.0) / total;
    
    // 确保分数在0-100范围内
    if (score < 0) score = 0;
    if (score > 100) score = 100;
    
    return score;
}

QVariantList DataCleaningPersistence::loadCleanedData(const QString& taskId)
{
    try {
        qDebug() << "DataCleaningPersistence: Loading cleaned data for task" << taskId;
        
        // 获取数据库连接
        auto connection = astock::database::ConnectionPool::instance().getConnection();
        if (!connection.isValid()) {
            qWarning() << "DataCleaningPersistence: Invalid database connection";
            return QVariantList();
        }
        
        // 获取任务ID
        int taskIdInt = getTaskId(connection, taskId);
        if (taskIdInt <= 0) {
            qWarning() << "DataCleaningPersistence: Task not found:" << taskId;
            return QVariantList();
        }
        
        // 查询清洗结果
        QSqlQuery query(connection);
        query.prepare(
            "SELECT symbol, trade_date, open, high, low, close, volume, turnover "
            "FROM cleaning_results "
            "WHERE task_id = :task_id AND is_cleaned = 1 "
            "ORDER BY trade_date"
        );
        query.bindValue(":task_id", taskIdInt);
        
        if (!query.exec()) {
            qWarning() << "DataCleaningPersistence: Failed to load cleaned data:" 
                       << query.lastError().text();
            return QVariantList();
        }
        
        QVariantList result;
        while (query.next()) {
            QVariantMap record;
            record["symbol"] = query.value(0).toString();
            record["date"] = query.value(1).toString();
            record["open"] = query.value(2).toDouble();
            record["high"] = query.value(3).toDouble();
            record["low"] = query.value(4).toDouble();
            record["close"] = query.value(5).toDouble();
            record["volume"] = query.value(6).toInt();
            record["turnover"] = query.value(7).toDouble();
            
            result.append(record);
        }
        
        qDebug() << "DataCleaningPersistence: Loaded" << result.size() << "cleaned records";
        return result;
        
    } catch (const std::exception& e) {
        qWarning() << "DataCleaningPersistence: Error loading cleaned data:" << e.what();
        return QVariantList();
    }
}

QVariantMap DataCleaningPersistence::getTaskStats(const QString& taskId)
{
    try {
        // 获取数据库连接
        auto connection = astock::database::ConnectionPool::instance().getConnection();
        if (!connection.isValid()) {
            qWarning() << "DataCleaningPersistence: Invalid database connection";
            return QVariantMap();
        }
        
        QSqlQuery query(connection);
        query.prepare(
            "SELECT original_record_count, cleaned_record_count, removed_record_count, "
            "data_quality_score, status, start_time, end_time, duration_seconds "
            "FROM cleaning_tasks "
            "WHERE task_uuid = :task_uuid"
        );
        query.bindValue(":task_uuid", taskId);
        
        if (!query.exec()) {
            qWarning() << "DataCleaningPersistence: Failed to get task stats:" 
                       << query.lastError().text();
            return QVariantMap();
        }
        
        if (query.next()) {
            QVariantMap stats;
            stats["totalRecords"] = query.value(0).toInt();
            stats["cleanedRecords"] = query.value(1).toInt();
            stats["removedRecords"] = query.value(2).toInt();
            stats["qualityScore"] = query.value(3).toDouble();
            stats["status"] = query.value(4).toString();
            stats["startTime"] = query.value(5).toDateTime();
            stats["endTime"] = query.value(6).toDateTime();
            stats["durationMs"] = query.value(7).toInt() * 1000;
            
            return stats;
        }
        
        return QVariantMap();
        
    } catch (const std::exception& e) {
        qWarning() << "DataCleaningPersistence: Error getting task stats:" << e.what();
        return QVariantMap();
    }
}

} // namespace ui::bridge