// DataFetchController.h - 改进版本，支持模型和数据缓存
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QDateTime>
#include <QTimer>

// 前向声明
class DataService;
class CleaningResultModel;

// 数据获取控制器 - QML与C++的纯转发桥接
// 不执行任何耗时操作，只负责转发请求和信号
class DataFetchController : public QObject {
    Q_OBJECT
    
    // QML属性
    Q_PROPERTY(QString dataSource READ dataSource WRITE setDataSource NOTIFY dataSourceChanged)
    Q_PROPERTY(QStringList symbols READ symbols WRITE setSymbols NOTIFY symbolsChanged)
    Q_PROPERTY(QString startDate READ startDate WRITE setStartDate NOTIFY startDateChanged)
    Q_PROPERTY(QString endDate READ endDate WRITE setEndDate NOTIFY endDateChanged)
    Q_PROPERTY(QString dataType READ dataType WRITE setDataType NOTIFY dataTypeChanged)
    Q_PROPERTY(bool isFetching READ isFetching NOTIFY isFetchingChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QVariantList fetchedData READ fetchedData NOTIFY fetchedDataChanged)
    Q_PROPERTY(CleaningResultModel* cleaningResultModel READ cleaningResultModel CONSTANT)
    
public:
    explicit DataFetchController(QObject* parent = nullptr);
    ~DataFetchController();
    
    // QML可调用的方法 - 纯转发，不执行耗时操作
    Q_INVOKABLE void fetchData();
    Q_INVOKABLE void cancelFetch();
    Q_INVOKABLE void clearData();
    Q_INVOKABLE void saveToDatabase();
    Q_INVOKABLE void loadFromDatabase(const QString& symbol, const QString& startDate, const QString& endDate);
    Q_INVOKABLE QVariantList cleanData(const QVariantList& data, const QVariantMap& rules);  // 同步清洗（小数据集）
    Q_INVOKABLE void cleanDataAsync(const QVariantMap& rules);     // 异步清洗（大数据集）
    
    // 数据源添加并自动加载数据
    Q_INVOKABLE void addDataSourceAndLoad(const QString& provider, const QString& market, 
                                         const QStringList& symbols, const QString& startDate, 
                                         const QString& endDate, const QString& dataType);
    
    // 属性getter/setter
    QString dataSource() const { return m_dataSource; }
    void setDataSource(const QString& source);
    
    QStringList symbols() const { return m_symbols; }
    void setSymbols(const QStringList& symbols);
    
    QString startDate() const { return m_startDate; }
    void setStartDate(const QString& date);
    
    QString endDate() const { return m_endDate; }
    void setEndDate(const QString& date);
    
    QString dataType() const { return m_dataType; }
    void setDataType(const QString& type);
    
    bool isFetching() const { return m_isFetching; }
    
    int progress() const { return m_progress; }
    
    QString statusMessage() const { return m_statusMessage; }
    
    QVariantList fetchedData() const { return m_fetchedData; }
    
    // 模型访问器
    CleaningResultModel* cleaningResultModel() const { return m_cleaningResultModel; }
    
signals:
    void dataSourceChanged();
    void symbolsChanged();
    void startDateChanged();
    void endDateChanged();
    void dataTypeChanged();
    void isFetchingChanged();
    void progressChanged();
    void statusMessageChanged();
    void fetchedDataChanged();
    
    // 通知信号 - 转发给QML
    void dataFetchStarted();
    void dataFetchProgress(int progress, const QString& message);
    void dataFetchCompleted(bool success, const QString& message, int dataCount);
    void dataFetchError(const QString& error);
    void dataSavedToDatabase(bool success, const QString& message);
    void dataLoadedFromDatabase(bool success, const QString& message, int dataCount);
    
    // 数据清洗信号 - 转发给QML
    void dataCleaningStarted();
    void dataCleaningProgress(int progress, const QString& message);
    void dataCleaningCompleted(bool success, const QString& message, const QVariantList& cleanedData);
    void dataCleaningError(const QString& error);
    
    // 内部请求信号 - 转发给DataService
    void requestFetchData(const QStringList& symbols, const QString& startDate, const QString& endDate);
    void requestSaveData(const QVariantList& data);
    void requestLoadData(const QString& symbol, const QString& startDate, const QString& endDate);
    void requestCleanData(const QVariantList& data, const QVariantMap& rules);
    void requestCancelOperation();
    
    // 内部槽函数 - 接收DataService的结果
public slots:
    void onDataLoadProgress(int progress, const QString& message);
    void onDataLoadCompleted(bool success, const QString& message, const QVariantList& data);
    void onDataLoadError(const QString& error);
    void onDataCleaningProgress(int progress, const QString& message);
    void onDataCleaningCompleted(bool success, const QString& message, const QVariantList& cleanedData);
    void onDataCleaningError(const QString& error);
    
private:
    // 更新状态（快速操作）
    void updateStatus(const QString& message, int progress = -1);
    
    // 更新清洗结果模型
    void updateCleaningResultModel(const QVariantList& cleanedData);
    
private:
    // DataService实例
    DataService* m_dataService;
    
    // 模型实例
    CleaningResultModel* m_cleaningResultModel;
    
    // QML属性
    QString m_dataSource{"juejin"};  // 数据源: juejin, akshare, tushare
    QStringList m_symbols{"600000.SH", "000001.SZ"};
    QString m_startDate;
    QString m_endDate;
    QString m_dataType{"daily"};  // 数据类型: daily, minute, tick
    bool m_isFetching{false};
    int m_progress{0};
    QString m_statusMessage{"就绪"};
    QVariantList m_fetchedData;
    
    // 当前加载的数据标识
    QString m_currentSymbol;
    QString m_currentStartDate;
    QString m_currentEndDate;
};