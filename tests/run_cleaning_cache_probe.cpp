#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantList>
#include <iostream>

#include "DataFetchController.h"
#include "DataCacheAdapter.h"
#include "foundation.h"

namespace {

QString detectRepoRoot()
{
    QDir dir(QDir::currentPath());
    for (int depth = 0; depth < 8; ++depth) {
        if (dir.exists(QStringLiteral("config")) && dir.exists(QStringLiteral("src"))
            && dir.exists(QStringLiteral("tests"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }

    dir = QDir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 8; ++depth) {
        if (dir.exists(QStringLiteral("config")) && dir.exists(QStringLiteral("src"))
            && dir.exists(QStringLiteral("tests"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }

    return QDir::currentPath();
}

int maxDataSetId(const QVector<QVariantMap>& infos)
{
    int maxId = 0;
    for (const auto& info : infos) {
        if (info.value("id").toInt() > maxId) {
            maxId = info.value("id").toInt();
        }
    }
    return maxId;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("astockquantapp-exe"));
    QStandardPaths::setTestModeEnabled(false);

    const QString repoRoot = detectRepoRoot();
    QDir::setCurrent(repoRoot);

    foundation::Config foundationConfig;
    foundationConfig.profile = "development";
    foundationConfig.config_dir = QDir(repoRoot).filePath(QStringLiteral("config")).toStdString();
    foundationConfig.enable_console_log = true;
    foundationConfig.enable_file_log = false;
    foundationConfig.thread_pool_size = 2;

    if (!foundation::Foundation::instance().initialize(foundationConfig)) {
        std::cerr << "foundation initialize failed\n";
        return 1;
    }

    DataCacheAdapter& cache = DataCacheAdapter::instance();
    if (!cache.isInitialized()) {
        std::cerr << "cache initialize failed\n";
        foundation::Foundation::instance().shutdown();
        return 1;
    }

    const QVector<QVariantMap> infosBefore = cache.getAllDataSetInfos();
    const int maxIdBefore = maxDataSetId(infosBefore);
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString dataSetRoot = QDir(baseDir).filePath(QStringLiteral("datasets"));

    std::cout << "cache_root=" << dataSetRoot.toStdString() << "\n";
    std::cout << "before_count=" << infosBefore.size() << " max_id=" << maxIdBefore << "\n";

    DataFetchController controller;
    controller.setDataSource(QStringLiteral("cache_probe"));
    controller.setSymbols(QStringList{QStringLiteral("CACHE_PROBE")});
    controller.setStartDate(QStringLiteral("2024-01-02"));
    controller.setEndDate(QStringLiteral("2024-01-02"));
   // controller.setDataType(QStringLiteral("daily"));

    QVariantList cleanedData;
    cleanedData.append(QVariantMap{
        {QStringLiteral("symbol"), QStringLiteral("CACHE_PROBE")},
        {QStringLiteral("trade_date"), QStringLiteral("2024-01-02")},
        {QStringLiteral("open"), 9.8},
        {QStringLiteral("high"), 10.2},
        {QStringLiteral("low"), 9.7},
        {QStringLiteral("close"), 10.0},
        {QStringLiteral("volume"), 1000.0},
        {QStringLiteral("turnover"), 12000.0},
        {QStringLiteral("adj_factor"), 1.25}
    });

    bool completed = false;
    bool successSignal = false;
    QString completionMessage;
    QEventLoop loop;

    QObject::connect(&controller,
                     &DataFetchController::dataCleaningCompleted,
                     &loop,
                     [&](bool success, const QString& message, const QVariantList&) {
                         completed = true;
                         successSignal = success;
                         completionMessage = message;
                         loop.quit();
                     });

    controller.onDataCleaningCompleted(true,
                                       QStringLiteral("缓存探针: 原始 1 条 -> 清洗后 1 条"),
                                       cleanedData);

    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();

    const QVector<QVariantMap> infosAfter = cache.getAllDataSetInfos();
    const int maxIdAfter = maxDataSetId(infosAfter);

    std::cout << "completed=" << (completed ? "true" : "false")
              << " success=" << (successSignal ? "true" : "false")
              << " message=" << completionMessage.toStdString() << "\n";
    std::cout << "after_count=" << infosAfter.size() << " max_id=" << maxIdAfter << "\n";

    if (!completed || !successSignal || infosAfter.size() <= infosBefore.size() || maxIdAfter <= maxIdBefore) {
        foundation::Foundation::instance().shutdown();
        return 2;
    }

    const QVariantMap latestInfo = cache.getDataSetInfo(maxIdAfter);
    std::cout << "new_dataset_id=" << latestInfo.value("id").toInt()
              << " sourceType=" << latestInfo.value("sourceType").toString().toStdString()
              << " rowCount=" << latestInfo.value("rowCount").toInt()
              << " displayName=" << latestInfo.value("displayName").toString().toStdString() << "\n";

    foundation::Foundation::instance().shutdown();
    return 0;
}