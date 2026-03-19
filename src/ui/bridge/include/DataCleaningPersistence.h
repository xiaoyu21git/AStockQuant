#ifndef DATACLEANINGPERSISTENCE_H
#define DATACLEANINGPERSISTENCE_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QSqlDatabase>

namespace ui::bridge {

class DataCleaningPersistence : public QObject
{
    Q_OBJECT

public:
    explicit DataCleaningPersistence(QObject* parent = nullptr);
    ~DataCleaningPersistence();

    // 保存清洗结果到数据库
    bool saveCleaningResult(const QString& taskId, 
                           const QVariantList& cleanedData,
                           const QVariantMap& stats);

    // 从数据库加载清洗结果
    QVariantList loadCleanedData(const QString& taskId);

    // 获取任务统计信息
    QVariantMap getTaskStats(const QString& taskId);

signals:
    void dataSaved(const QString& taskId);
    void dataLoaded(const QString& taskId, const QVariantList& data);

private:
    // 保存清洗任务记录
    bool saveCleaningTask(QSqlDatabase& connection, 
                         const QString& taskId,
                         const QVariantMap& stats);

    // 保存清洗后的数据
    bool saveCleanedData(QSqlDatabase& connection,
                        const QString& taskId,
                        const QVariantList& cleanedData);

    // 获取任务ID
    int getTaskId(QSqlDatabase& connection, const QString& taskUuid);

    // 计算数据质量评分
    double calculateQualityScore(const QVariantMap& stats);
};

} // namespace ui::bridge

#endif // DATACLEANINGPERSISTENCE_H