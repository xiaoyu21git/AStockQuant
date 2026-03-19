#include "FactorBacktestController.h"
#include "../../../domain/backtest/include/FactorBacktestService.h"
#include "../../../domain/backtest/include/FactorBacktestTypes.h"
#include "../../../domain/backtest/include/DatabaseStockDataProvider.h"
#include "../../../domain/backtest/include/DatabaseFactorDataProvider.h"
#include "../../../foundation/include/foundation.h"
#include "../include/FactorService.h"
#include "../include/DataServiceCache.h"
#include <QTimer>
#include <QDebug>
#include <thread>
#include <chrono>

using namespace domain::backtest;

FactorBacktestController::FactorBacktestController(QObject *parent)
    : QObject(parent)
    , m_isRunning(false)
    , m_progress(0)
    , m_status("未初始化")
{
    qDebug() << "FactorBacktestController 创建";
}

FactorBacktestController::~FactorBacktestController()
{
    qDebug() << "FactorBacktestController 销毁";
}

void FactorBacktestController::setSelectedFactorIds(const QVariantList& factorIds)
{
    if (m_selectedFactorIds != factorIds) {
        m_selectedFactorIds = factorIds;
        emit selectedFactorIdsChanged(m_selectedFactorIds);
        qDebug() << "FactorBacktestController: 更新选择的因子ID，数量:" << m_selectedFactorIds.size();
    }
}

void FactorBacktestController::startBacktest(const QString& groupText, 
                                             const QString& startDate, 
                                             const QString& endDate)
{
    // 使用控制器内部存储的因子ID
    startBacktestWithFactors(m_selectedFactorIds, groupText, startDate, endDate);
}

void FactorBacktestController::startBacktestWithFactors(
    const QVariantList& factorIds,
    const QString& groupText,
    const QString& startDate,
    const QString& endDate)
{
    qDebug() << "开始回测，因子数量:" << factorIds.size() << "分组:" << groupText;
    
    if (factorIds.isEmpty()) {
        qWarning() << "请选择至少一个因子";
        return;
    }
    
    if (m_isRunning) {
        qWarning() << "已有回测任务正在运行";
        return;
    }
    
    // 设置运行状态
    m_isRunning = true;
    m_progress = 0;
    m_status = "正在回测";
    
    emit isRunningChanged(m_isRunning);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    
    // 解析分组数量
    int groupCount = parseGroupCount(groupText);
    qDebug() << "分组数量:" << groupCount;
    
    qDebug() << "回测直接从缓存获取数据，不依赖日期参数";
    
    // 启动回测任务
    std::thread([this, factorIds, groupCount]() {
        try {
            // 从缓存获取数据集信息
            DataServiceCache& cache = DataServiceCache::getInstance();
            QVector<DataServiceCache::DataSetInfo> dataSets = cache.getAllDataSetInfos();
            
            if (dataSets.isEmpty()) {
                throw std::runtime_error("缓存中没有找到清洗后的数据集，请先进行数据清洗");
            }
            
            // 获取第一个清洗后数据集的日期范围
            QString cacheStartDate = "";
            QString cacheEndDate = "";
            
            for (const auto& ds : dataSets) {
                if (ds.startDate.isValid() && ds.endDate.isValid()) {
                    cacheStartDate = ds.startDate.toString("yyyy-MM-dd");
                    cacheEndDate = ds.endDate.toString("yyyy-MM-dd");
                    qDebug() << "从缓存数据集获取日期范围:" << ds.displayName 
                             << "=>" << cacheStartDate << "至" << cacheEndDate;
                    break;
                }
            }
            
            if (cacheStartDate.isEmpty() || cacheEndDate.isEmpty()) {
                throw std::runtime_error("缓存数据集没有有效的日期范围");
            }
            
            // 创建配置 - 使用缓存中的日期范围
            FactorBacktestConfig config;
            config.startDate = cacheStartDate.toStdString();
            config.endDate = cacheEndDate.toStdString();
            config.numGroups = groupCount;
            config.initialCapital = 1000000;
            config.transactionCost = 0.001;
            config.slippage = 0.001;
            config.maxThreads = 4;
            config.enableCache = true;
            config.cacheTTL = 3600;
            
            qDebug() << "使用缓存日期范围进行回测:" << cacheStartDate << "至" << cacheEndDate;
            
            // 更新进度
            for (int i = 0; i <= 100; i += 10) {
                if (!m_isRunning) break;
                
                // 使用简单的lambda更新进度
                QMetaObject::invokeMethod(this, [this, i]() {
                    this->m_progress = i;
                    this->m_status = QString("正在回测... %1%").arg(i);
                    emit progressChanged(this->m_progress);
                    emit statusChanged(this->m_status);
                    emit backtestProgress(i, this->m_status);
                }, Qt::QueuedConnection);
                
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            if (!m_isRunning) {
                // 回测被取消
                QMetaObject::invokeMethod(this, [this]() {
                    m_isRunning = false;
                    m_status = "已取消";
                    emit isRunningChanged(m_isRunning);
                    emit statusChanged(m_status);
                    emit backtestCancelled();
                }, Qt::QueuedConnection);
                return;
            }
            
            // 运行回测（这里简化处理，只回测第一个因子）
            if (!factorIds.isEmpty()) {
                QString firstFactorId = factorIds.first().toString();
                config.factorId = firstFactorId.toStdString();
                config.factorName = firstFactorId.toStdString();
                
                // 发射开始信号
                QMetaObject::invokeMethod(this, [this, firstFactorId]() {
                    emit backtestStarted(firstFactorId);
                }, Qt::QueuedConnection);
                
                // 初始化服务（如果需要）
                if (!m_service) {
                    // 初始化foundation（如果尚未初始化）
                    if (!foundation::Foundation::instance().is_initialized()) {
                        foundation::Config config;
                        config.thread_pool_size = 4;
                        foundation::Foundation::instance().initialize(config);
                    }
                    
                    // 创建回测服务
                    m_service = std::make_unique<FactorBacktestService>();
                    
                    // 使用真实的数据提供器
                    auto stockDataProvider = std::make_shared<DatabaseStockDataProvider>(nullptr);
                    
                    // 获取FactorService单例实例
                    auto factorService = FactorService::instance();
                    if (!factorService) {
                        throw std::runtime_error("无法获取FactorService实例");
                    }
                    
                    // 确保FactorService已初始化
                    factorService->initialize();
                    
                    // 创建DatabaseFactorDataProvider，传递FactorService实例
                    auto factorDataProvider = std::make_shared<DatabaseFactorDataProvider>(
                        std::shared_ptr<FactorService>(factorService, [](FactorService*) {})
                    );
                    
                    // 设置数据提供器
                    m_service->setStockDataProvider(stockDataProvider);
                    m_service->setFactorDataProvider(factorDataProvider);
                    
                    qDebug() << "✅ 因子回测服务初始化成功";
                }
                
                // 运行回测
                FactorBacktestResult result = m_service->runFactorBacktestSync(config);
                
                // 转换结果
                QVariantMap resultMap;
                resultMap["taskId"] = QString::fromStdString(result.taskId);
                resultMap["executionTime"] = result.executionTime;
                resultMap["success"] = true;
                
                // 配置信息
                QVariantMap configMap;
                configMap["factorId"] = QString::fromStdString(result.config.factorId);
                configMap["factorName"] = QString::fromStdString(result.config.factorName);
                configMap["startDate"] = QString::fromStdString(result.config.startDate);
                configMap["endDate"] = QString::fromStdString(result.config.endDate);
                configMap["numGroups"] = result.config.numGroups;
                configMap["initialCapital"] = result.config.initialCapital;
                resultMap["config"] = configMap;
                
                // 分组信息
                QVariantList groups;
                for (size_t i = 0; i < result.groups.size(); ++i) {
                    const auto& group = result.groups[i];
                    QVariantMap groupMap;
                    groupMap["groupId"] = group.groupId;
                    groupMap["groupName"] = QString::fromStdString(group.groupName);
                    groupMap["minFactorValue"] = group.minFactorValue;
                    groupMap["maxFactorValue"] = group.maxFactorValue;
                    groupMap["stockCount"] = group.stockCount;
                    
                    // 从groupBacktestResults中获取完整的回测结果
                    if (i < result.groupBacktestResults.size()) {
                        const auto& backtestResult = result.groupBacktestResults[i];
                        const auto& perf = backtestResult.performance();
                        const auto& risk = backtestResult.risk_metrics();
                        const auto& trade = backtestResult.trade_stats();
                        
                        groupMap["return"] = perf.total_return;
                        groupMap["annualizedReturn"] = perf.annual_return;  // 注意：这里是annual_return不是annualized_return
                        groupMap["maxDrawdown"] = risk.max_drawdown;
                        groupMap["sharpeRatio"] = risk.sharpe_ratio;
                        groupMap["winRate"] = trade.win_rate;
                        groupMap["volatility"] = risk.volatility;
                        
                        // 设置其他指标
                        groupMap["profitFactor"] = trade.profit_factor;
                        groupMap["calmarRatio"] = risk.calmar_ratio;
                        groupMap["sortinoRatio"] = risk.sortino_ratio;
                        groupMap["alpha"] = perf.alpha;
                        groupMap["beta"] = perf.beta;
                        groupMap["informationRatio"] = perf.information_ratio;
                        
                        // trackingError需要计算，这里使用默认值
                        groupMap["trackingError"] = 0.0;
                    } else {
                        // 设置默认值
                        groupMap["return"] = 0.0;
                        groupMap["annualizedReturn"] = 0.0;
                        groupMap["volatility"] = 0.0;
                        groupMap["sharpeRatio"] = 0.0;
                        groupMap["maxDrawdown"] = 0.0;
                        groupMap["winRate"] = 0.0;
                        groupMap["profitFactor"] = 0.0;
                        groupMap["calmarRatio"] = 0.0;
                        groupMap["sortinoRatio"] = 0.0;
                        groupMap["alpha"] = 0.0;
                        groupMap["beta"] = 0.0;
                        groupMap["trackingError"] = 0.0;
                        groupMap["informationRatio"] = 0.0;
                    }
                    
                    groups.append(groupMap);
                }
                resultMap["groups"] = groups;
                
                // ICIR信息 - 提取完整的ICIR结果
                QVariantMap icirMap;
                icirMap["icValue"] = result.icirResult.icValue;
                icirMap["irValue"] = result.icirResult.irValue;
                icirMap["icTStat"] = result.icirResult.icTStat;
                icirMap["icPValue"] = result.icirResult.icPValue;
                icirMap["icPositiveRate"] = result.icirResult.icPositiveRate;
                icirMap["isSignificant"] = result.icirResult.isSignificant;
                
                // 将IC时间序列转换为QVariantList
                QVariantList icSeriesList;
                for (const auto& ic : result.icirResult.icSeries) {
                    icSeriesList.append(ic);
                }
                icirMap["icSeries"] = icSeriesList;
                
                // 将IR时间序列转换为QVariantList
                QVariantList irSeriesList;
                for (const auto& ir : result.icirResult.irSeries) {
                    irSeriesList.append(ir);
                }
                icirMap["irSeries"] = irSeriesList;
                
                icirMap["conclusion"] = QString("IC值: %1, IR值: %2, IC T统计: %3, IC正率: %4%")
                    .arg(result.icirResult.icValue, 0, 'f', 3)
                    .arg(result.icirResult.irValue, 0, 'f', 2)
                    .arg(result.icirResult.icTStat, 0, 'f', 2)
                    .arg(result.icirResult.icPositiveRate * 100, 0, 'f', 1);
                resultMap["icirResult"] = icirMap;
                
                // 汇总统计
                QVariantMap summaryMap;
                summaryMap["topGroupReturn"] = result.summary.topGroupReturn;
                summaryMap["bottomGroupReturn"] = result.summary.bottomGroupReturn;
                summaryMap["spreadReturn"] = result.summary.spreadReturn;
                summaryMap["monotonicity"] = result.summary.monotonicity;
                summaryMap["discrimination"] = result.summary.discrimination;
                summaryMap["winRate"] = result.summary.winRate;
                summaryMap["sharpeRatio"] = result.summary.sharpeRatio;
                summaryMap["maxDrawdown"] = result.summary.maxDrawdown;
                resultMap["summary"] = summaryMap;
                
                // 更新结果
                m_groupResults = groups;
                m_icirResult = icirMap;
                m_summaryStats = summaryMap;
                
                // 发送完成信号
                QMetaObject::invokeMethod(this, [this, resultMap]() {
                    m_isRunning = false;
                    m_progress = 100;
                    m_status = "回测完成";
                    
                    emit isRunningChanged(m_isRunning);
                    emit progressChanged(m_progress);
                    emit statusChanged(m_status);
                    emit groupResultsChanged(m_groupResults);
                    emit icirResultChanged(m_icirResult);
                    emit summaryStatsChanged(m_summaryStats);
                    emit backtestCompleted(resultMap);
                    
                    qDebug() << "回测完成";
                }, Qt::QueuedConnection);
                
            } else {
                throw std::runtime_error("没有有效的因子");
            }
            
        } catch (const std::exception& e) {
            QString error = QString("回测失败: %1").arg(e.what());
            QMetaObject::invokeMethod(this, [this, error]() {
                m_isRunning = false;
                m_status = "回测失败";
                emit isRunningChanged(m_isRunning);
                emit statusChanged(m_status);
                emit backtestFailed(error);
                qCritical() << "回测失败:" << error;
            }, Qt::QueuedConnection);
        }
    }).detach();
}

void FactorBacktestController::cancelBacktest()
{
    qDebug() << "取消回测";
    
    if (!m_isRunning) {
        return;
    }
    
    m_isRunning = false;
    m_status = "正在取消...";
    
    emit isRunningChanged(m_isRunning);
    emit statusChanged(m_status);
    
    // 注意：这里简化处理，实际应该停止正在运行的线程
    // 由于我们使用了简单的线程，这里只是设置标志让线程自己检查
}

int FactorBacktestController::parseGroupCount(const QString& groupText) const
{
    if (groupText.contains("5")) return 5;
    if (groupText.contains("10")) return 10;
    if (groupText.contains("20")) return 20;
    return 10; // 默认值
}

QVariantMap FactorBacktestController::getDefaultDateRange() const
{
    QVariantMap dateRange;
    
    // 回测直接从缓存获取数据，不需要日期范围
    // 这个函数现在只返回空值，因为日期范围从缓存中获取
    qDebug() << "FactorBacktestController::getDefaultDateRange: 回测直接从缓存获取数据，返回空日期范围";
    
    dateRange["startDate"] = "";
    dateRange["endDate"] = "";
    return dateRange;
}
