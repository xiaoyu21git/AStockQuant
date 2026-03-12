// GlobalDataService.h
// 全局数据服务 - 单例模式，提供系统状态、通知等共享数据
#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QMutex>
#include <QMutexLocker>

class GlobalDataService : public QObject {
    Q_OBJECT
    
    // 系统状态属性
    Q_PROPERTY(QVariantMap systemStatus READ systemStatus NOTIFY systemStatusChanged)
    Q_PROPERTY(QVariantList notifications READ notifications NOTIFY notificationsChanged)
    
    // 模板数据
    Q_PROPERTY(QVariantList templates READ templates NOTIFY templatesChanged)
    
    // 历史记录数据
    Q_PROPERTY(QVariantList historyRecords READ historyRecords NOTIFY historyRecordsChanged)
    
    // 最近分析数据
    Q_PROPERTY(QVariantList recentAnalysis READ recentAnalysis NOTIFY recentAnalysisChanged)
    
public:
    // 单例访问
    static GlobalDataService* instance();
    
    // QML可调用方法
    Q_INVOKABLE void initialize();
    Q_INVOKABLE void updateSystemStatus(const QString& key, const QVariant& value);
    Q_INVOKABLE void addNotification(const QVariantMap& notification);
    Q_INVOKABLE void clearNotifications();
    Q_INVOKABLE QVariantMap getTemplateById(const QString& templateId);
    Q_INVOKABLE QVariantMap getHistoryRecordById(const QString& recordId);
    Q_INVOKABLE void addHistoryRecord(const QVariantMap& record);
    Q_INVOKABLE void addRecentAnalysis(const QVariantMap& analysis);
    
    // 属性访问器
    QVariantMap systemStatus() const;
    QVariantList notifications() const;
    QVariantList templates() const;
    QVariantList historyRecords() const;
    QVariantList recentAnalysis() const;
    
signals:
    void systemStatusChanged();
    void notificationsChanged();
    void templatesChanged();
    void historyRecordsChanged();
    void recentAnalysisChanged();
    
    void dataInitialized();
    void errorOccurred(const QString& error);
    
private:
    GlobalDataService(QObject* parent = nullptr);
    ~GlobalDataService();
    
    // 禁止拷贝
    GlobalDataService(const GlobalDataService&) = delete;
    GlobalDataService& operator=(const GlobalDataService&) = delete;
    
    // 初始化默认数据
    void initializeDefaultData();
    void initializeSystemStatus();
    void initializeNotifications();
    void initializeTemplates();
    void initializeHistoryRecords();
    void initializeRecentAnalysis();
    
    // 数据查找辅助函数
    QVariantMap findInList(const QVariantList& list, const QString& idKey, const QString& idValue) const;
    
private:
    // 单例实例
    static GlobalDataService* m_instance;
    static QMutex m_mutex;
    
    // 数据存储
    QVariantMap m_systemStatus;
    QVariantList m_notifications;
    QVariantList m_templates;
    QVariantList m_historyRecords;
    QVariantList m_recentAnalysis;
    
    // 互斥锁
    mutable QMutex m_dataMutex;
};