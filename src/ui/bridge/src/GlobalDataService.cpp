// GlobalDataService.cpp
// 全局数据服务实现

#include "../../ui/bridge/include/GlobalDataService.h"
#include <QDateTime>
#include <QDebug>

// 静态成员初始化
GlobalDataService* GlobalDataService::m_instance = nullptr;
QMutex GlobalDataService::m_mutex;

GlobalDataService::GlobalDataService(QObject* parent)
    : QObject(parent)
{
    qDebug() << "GlobalDataService constructor";
}

GlobalDataService::~GlobalDataService()
{
    qDebug() << "GlobalDataService destructor";
}

GlobalDataService* GlobalDataService::instance()
{
    if (m_instance == nullptr) {
        QMutexLocker locker(&m_mutex);
        if (m_instance == nullptr) {
            m_instance = new GlobalDataService();
        }
    }
    return m_instance;
}

void GlobalDataService::initialize()
{
    QMutexLocker locker(&m_dataMutex);
    
    try {
        initializeDefaultData();
        emit dataInitialized();
        qDebug() << "GlobalDataService initialized successfully";
    } catch (const std::exception& e) {
        QString error = QString("Failed to initialize GlobalDataService: %1").arg(e.what());
        qCritical() << error;
        emit errorOccurred(error);
    }
}

void GlobalDataService::updateSystemStatus(const QString& key, const QVariant& value)
{
    QMutexLocker locker(&m_dataMutex);
    
    m_systemStatus[key] = value;
    locker.unlock();
    
    emit systemStatusChanged();
    qDebug() << "System status updated:" << key << "=" << value;
}

void GlobalDataService::addNotification(const QVariantMap& notification)
{
    QMutexLocker locker(&m_dataMutex);
    
    // 添加时间戳
    QVariantMap notificationWithTime = notification;
    if (!notificationWithTime.contains("time")) {
        notificationWithTime["time"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    }
    
    m_notifications.prepend(notificationWithTime);
    
    // 限制通知数量
    if (m_notifications.size() > 20) {
        m_notifications = m_notifications.mid(0, 20);
    }
    
    locker.unlock();
    
    emit notificationsChanged();
    qDebug() << "Notification added:" << notificationWithTime["text"];
}

void GlobalDataService::clearNotifications()
{
    QMutexLocker locker(&m_dataMutex);
    
    m_notifications.clear();
    locker.unlock();
    
    emit notificationsChanged();
    qDebug() << "Notifications cleared";
}

QVariantMap GlobalDataService::getTemplateById(const QString& templateId)
{
    QMutexLocker locker(&m_dataMutex);
    return findInList(m_templates, "templateId", templateId);
}

QVariantMap GlobalDataService::getHistoryRecordById(const QString& recordId)
{
    QMutexLocker locker(&m_dataMutex);
    return findInList(m_historyRecords, "recordId", recordId);
}

void GlobalDataService::addHistoryRecord(const QVariantMap& record)
{
    QMutexLocker locker(&m_dataMutex);
    
    QVariantMap recordWithId = record;
    if (!recordWithId.contains("recordId")) {
        recordWithId["recordId"] = QString("hist_%1").arg(QDateTime::currentMSecsSinceEpoch());
    }
    
    m_historyRecords.prepend(recordWithId);
    
    // 限制历史记录数量
    if (m_historyRecords.size() > 50) {
        m_historyRecords = m_historyRecords.mid(0, 50);
    }
    
    locker.unlock();
    
    emit historyRecordsChanged();
    qDebug() << "History record added:" << recordWithId["recordId"];
}

void GlobalDataService::addRecentAnalysis(const QVariantMap& analysis)
{
    QMutexLocker locker(&m_dataMutex);
    
    QVariantMap analysisWithTime = analysis;
    if (!analysisWithTime.contains("time")) {
        analysisWithTime["time"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    }
    
    m_recentAnalysis.prepend(analysisWithTime);
    
    // 限制最近分析数量
    if (m_recentAnalysis.size() > 10) {
        m_recentAnalysis = m_recentAnalysis.mid(0, 10);
    }
    
    locker.unlock();
    
    emit recentAnalysisChanged();
    qDebug() << "Recent analysis added:" << analysisWithTime["analysisId"];
}

QVariantMap GlobalDataService::systemStatus() const
{
    QMutexLocker locker(&m_dataMutex);
    return m_systemStatus;
}

QVariantList GlobalDataService::notifications() const
{
    QMutexLocker locker(&m_dataMutex);
    return m_notifications;
}

QVariantList GlobalDataService::templates() const
{
    QMutexLocker locker(&m_dataMutex);
    return m_templates;
}

QVariantList GlobalDataService::historyRecords() const
{
    QMutexLocker locker(&m_dataMutex);
    return m_historyRecords;
}

QVariantList GlobalDataService::recentAnalysis() const
{
    QMutexLocker locker(&m_dataMutex);
    return m_recentAnalysis;
}

void GlobalDataService::initializeDefaultData()
{
    initializeSystemStatus();
    initializeNotifications();
    initializeTemplates();
    initializeHistoryRecords();
    initializeRecentAnalysis();
}

void GlobalDataService::initializeSystemStatus()
{
    m_systemStatus = QVariantMap{
        {"计算资源", QVariantMap{{"status", "🟢"}, {"value", "3核空闲"}, {"color", "#10b981"}}},
        {"内存使用", QVariantMap{{"status", "📊"}, {"value", "62%"}, {"color", "#3b82f6"}}},
        {"数据处理", QVariantMap{{"status", "⚡"}, {"value", "实时"}, {"color", "#f59e0b"}}},
        {"网络状态", QVariantMap{{"status", "🟢"}, {"value", "正常"}, {"color", "#10b981"}}},
        {"存储空间", QVariantMap{{"status", "📊"}, {"value", "45%"}, {"color", "#3b82f6"}}}
    };
}

void GlobalDataService::initializeNotifications()
{
    m_notifications = QVariantList{
        QVariantMap{
            {"type", "info"},
            {"text", "因子MA_20回测完成"},
            {"time", "5分钟前"},
            {"action", "查看报告"}
        },
        QVariantMap{
            {"type", "warning"},
            {"text", "参数优化建议: 窗口期25"},
            {"time", "10分钟前"},
            {"action", "应用"}
        },
        QVariantMap{
            {"type", "success"},
            {"text", "2个新因子已入库"},
            {"time", "1小时前"},
            {"action", "查看"}
        },
        QVariantMap{
            {"type", "info"},
            {"text", "数据更新任务已启动"},
            {"time", "2小时前"},
            {"action", "监控"}
        }
    };
}

void GlobalDataService::initializeTemplates()
{
    m_templates = QVariantList{
        QVariantMap{
            {"templateId", "momentum_basic"},
            {"templateName", "基础动量模板"},
            {"category", "动量类"},
            {"description", "标准价格动量计算模板"},
            {"icon", "📊"},
            {"complexity", "简单"},
            {"esttime", "2分钟"}
        },
        QVariantMap{
            {"templateId", "value_smart"},
            {"templateName", "智能价值模板"},
            {"category", "价值类"},
            {"description", "行业中性化的估值因子模板"},
            {"icon", "💰"},
            {"complexity", "中等"},
            {"esttime", "5分钟"}
        },
        QVariantMap{
            {"templateId", "quality_advanced"},
            {"templateName", "高级质量模板"},
            {"category", "质量类"},
            {"description", "多维度财务质量分析模板"},
            {"icon", "📈"},
            {"complexity", "复杂"},
            {"esttime", "10分钟"}
        },
        QVariantMap{
            {"templateId", "technical_standard"},
            {"templateName", "标准技术模板"},
            {"category", "技术指标"},
            {"description", "常用技术指标计算模板"},
            {"icon", "📊"},
            {"complexity", "简单"},
            {"esttime", "3分钟"}
        },
        QVariantMap{
            {"templateId", "sentiment_basic"},
            {"templateName", "基础情绪模板"},
            {"category", "情绪类"},
            {"description", "市场情绪分析模板"},
            {"icon", "🧠"},
            {"complexity", "中等"},
            {"esttime", "8分钟"}
        }
    };
}

void GlobalDataService::initializeHistoryRecords()
{
    QString currentDate = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    
    m_historyRecords = QVariantList{
        QVariantMap{
            {"recordId", "hist_ma_20_optimized"},
            {"factorId", "ma_20_optimized"},
            {"factorName", "MA_20优化版"},
            {"time", currentDate + " 15:30"},
            {"operation", "参数优化"},
            {"stat", "success"}
        },
        QVariantMap{
            {"recordId", "hist_rsi_custom"},
            {"factorId", "rsi_custom"},
            {"factorName", "自定义RSI"},
            {"time", QDateTime::currentDateTime().addDays(-1).toString("yyyy-MM-dd") + " 10:20"},
            {"operation", "创建因子"},
            {"stat", "success"}
        },
        QVariantMap{
            {"recordId", "hist_pe_analysis"},
            {"factorId", "pe_ttm"},
            {"factorName", "PE_TTM分析"},
            {"time", currentDate + " 09:15"},
            {"operation", "IC分析"},
            {"stat", "success"}
        },
        QVariantMap{
            {"recordId", "hist_roe_test"},
            {"factorId", "roe_quality"},
            {"factorName", "ROE质量因子"},
            {"time", QDateTime::currentDateTime().addDays(-1).toString("yyyy-MM-dd") + " 14:40"},
            {"operation", "分组收益测试"},
            {"stat", "success"}
        }
    };
}

void GlobalDataService::initializeRecentAnalysis()
{
    QString currentDate = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    
    m_recentAnalysis = QVariantList{
        QVariantMap{
            {"analysisId", "analysis_pe_ttm"},
            {"factorId", "pe_ttm"},
            {"factorName", "PE_TTM"},
            {"analysisType", "IC分析"},
            {"time", currentDate + " 09:15"},
            {"result", "IC: 0.035"}
        },
        QVariantMap{
            {"analysisId", "analysis_roe_quality"},
            {"factorId", "roe_quality"},
            {"factorName", "ROE质量因子"},
            {"analysisType", "分组收益"},
            {"time", QDateTime::currentDateTime().addDays(-1).toString("yyyy-MM-dd") + " 14:40"},
            {"result", "单调性: ✓"}
        },
        QVariantMap{
            {"analysisId", "analysis_momentum_20d"},
            {"factorId", "momentum_20d"},
            {"factorName", "Momentum_20D"},
            {"analysisType", "稳定性分析"},
            {"time", currentDate + " 11:30"},
            {"result", "滚动IC稳定"}
        },
        QVariantMap{
            {"analysisId", "analysis_rsi_14"},
            {"factorId", "rsi_14"},
            {"factorName", "RSI_14"},
            {"analysisType", "回测分析"},
            {"time", QDateTime::currentDateTime().addDays(-2).toString("yyyy-MM-dd") + " 16:20"},
            {"result", "年化收益: 12.5%"}
        }
    };
}

QVariantMap GlobalDataService::findInList(const QVariantList& list, const QString& idKey, const QString& idValue) const
{
    for (const QVariant& item : list) {
        QVariantMap map = item.toMap();
        if (map.contains(idKey) && map[idKey].toString() == idValue) {
            return map;
        }
    }
    
    return QVariantMap();
}