#include "TradingMarketCalendarService.h"

#include "DatabaseConnectionManager.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>
#include <QTime>

#include <thread>

namespace {

constexpr int kMorningOpenMinutes = 9 * 60 + 15;
constexpr int kMorningCloseMinutes = 11 * 60 + 30;
constexpr int kAfternoonOpenMinutes = 13 * 60;
constexpr int kAfternoonCloseMinutes = 15 * 60;
constexpr int kMarketCloseMinutes = 15 * 60 + 30;

QString normalizeTradeDateText(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QDate parsed = QDate::fromString(trimmed, Qt::ISODate);
    return parsed.isValid() ? parsed.toString(QStringLiteral("yyyy-MM-dd")) : trimmed;
}

QString sourceLabel(const QString& source)
{
    if (source == QStringLiteral("akshare")) {
        return QStringLiteral("AKShare 交易日历");
    }
    if (source == QStringLiteral("fallback")) {
        return QStringLiteral("本地工作日回退");
    }
    return QStringLiteral("未知来源");
}

QString sessionPhaseCode(bool isTradingDay, const QTime& currentTime)
{
    if (!isTradingDay) {
        return QStringLiteral("HOLIDAY");
    }

    const int totalMinutes = currentTime.hour() * 60 + currentTime.minute();
    if (totalMinutes < kMorningOpenMinutes) {
        return QStringLiteral("PRE_OPEN");
    }
    if (totalMinutes < kMorningCloseMinutes) {
        return QStringLiteral("TRADING");
    }
    if (totalMinutes < kAfternoonOpenMinutes) {
        return QStringLiteral("LUNCH_BREAK");
    }
    if (totalMinutes < kAfternoonCloseMinutes) {
        return QStringLiteral("TRADING");
    }
    if (totalMinutes < kMarketCloseMinutes) {
        return QStringLiteral("POST_CLOSE");
    }
    return QStringLiteral("CLOSED");
}

QString sessionPhaseLabel(const QString& phase)
{
    if (phase == QStringLiteral("PRE_OPEN")) {
        return QStringLiteral("开盘前");
    }
    if (phase == QStringLiteral("TRADING")) {
        return QStringLiteral("交易中");
    }
    if (phase == QStringLiteral("LUNCH_BREAK")) {
        return QStringLiteral("午间休市");
    }
    if (phase == QStringLiteral("POST_CLOSE")) {
        return QStringLiteral("收盘后整理");
    }
    if (phase == QStringLiteral("CLOSED")) {
        return QStringLiteral("已收盘");
    }
    if (phase == QStringLiteral("HOLIDAY")) {
        return QStringLiteral("非交易日");
    }
    return QStringLiteral("未知");
}

QString previousWeekday(const QDate& date)
{
    QDate candidate = date.addDays(-1);
    while (candidate.dayOfWeek() >= 6) {
        candidate = candidate.addDays(-1);
    }
    return candidate.toString(QStringLiteral("yyyy-MM-dd"));
}

QString nextWeekday(const QDate& date)
{
    QDate candidate = date.addDays(1);
    while (candidate.dayOfWeek() >= 6) {
        candidate = candidate.addDays(1);
    }
    return candidate.toString(QStringLiteral("yyyy-MM-dd"));
}

QString queryPreviousKnownTradeDate(const QDate& today)
{
    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        return {};
    }

    const auto result = database->executeQuery(
        QStringLiteral("SELECT DISTINCT trade_date FROM daily_bar WHERE trade_date < :today ORDER BY trade_date DESC LIMIT 1"),
        {{QStringLiteral(":today"), today.toString(QStringLiteral("yyyy-MM-dd"))}}
    );
    if (result.isEmpty()) {
        return {};
    }

    return normalizeTradeDateText(result.getRow(0).getString("trade_date"));
}

QStringList candidateRepoRoots()
{
    QStringList candidates;
    candidates << QDir::currentPath();

    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        candidates << appDir;
        candidates << QDir(appDir).absoluteFilePath(QStringLiteral(".."));
        candidates << QDir(appDir).absoluteFilePath(QStringLiteral("../.."));
        candidates << QDir(appDir).absoluteFilePath(QStringLiteral("../../.."));
    }

    candidates.removeDuplicates();
    return candidates;
}

QString resolveRepoRoot()
{
    for (const QString& candidate : candidateRepoRoots()) {
        const QFileInfo scriptInfo(QDir(candidate).absoluteFilePath(QStringLiteral("tools/trading_day_utils.py")));
        if (scriptInfo.exists() && scriptInfo.isFile()) {
            return QDir(candidate).absolutePath();
        }
    }
    return {};
}

QString resolvePythonExecutable(const QString& repoRoot)
{
    if (!repoRoot.isEmpty()) {
        const QString windowsVenv = QDir(repoRoot).absoluteFilePath(QStringLiteral(".venv/Scripts/python.exe"));
        if (QFileInfo::exists(windowsVenv)) {
            return windowsVenv;
        }

        const QString unixVenv = QDir(repoRoot).absoluteFilePath(QStringLiteral(".venv/bin/python"));
        if (QFileInfo::exists(unixVenv)) {
            return unixVenv;
        }
    }

    return QStringLiteral("python");
}

QVariantMap parseCalendarBase(const QByteArray& standardOutput, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(standardOutput, &parseError);
    if (!document.isObject()) {
        if (error) {
            *error = QStringLiteral("交易日历返回了无效 JSON: %1").arg(parseError.errorString());
        }
        return {};
    }

    QVariantMap result = document.object().toVariantMap();
    result.insert(QStringLiteral("source"), QStringLiteral("akshare"));
    result.insert(QStringLiteral("sourceLabel"), sourceLabel(QStringLiteral("akshare")));
    result.insert(QStringLiteral("holidayAware"), true);
    result.insert(QStringLiteral("available"), true);
    result.insert(QStringLiteral("calendarDate"), normalizeTradeDateText(result.value(QStringLiteral("calendarDate")).toString()));
    result.insert(QStringLiteral("previousTradeDate"), normalizeTradeDateText(result.value(QStringLiteral("previousTradeDate")).toString()));
    result.insert(QStringLiteral("nextTradeDate"), normalizeTradeDateText(result.value(QStringLiteral("nextTradeDate")).toString()));
    return result;
}

QVariantMap loadCalendarBaseFromPython(const QString& repoRoot, QString* error)
{
    if (repoRoot.isEmpty()) {
        if (error) {
            *error = QStringLiteral("未找到项目根目录，无法加载交易日历工具");
        }
        return {};
    }

    static const QString script = QStringLiteral(
        "import datetime as dt, json\n"
        "from tools.trading_day_utils import get_trade_calendar\n"
        "calendar = get_trade_calendar()\n"
        "today = dt.date.today()\n"
        "previous_trade = None\n"
        "next_trade = None\n"
        "is_trading_day = False\n"
        "for trade_date in calendar:\n"
        "    if trade_date < today:\n"
        "        previous_trade = trade_date\n"
        "        continue\n"
        "    if trade_date == today:\n"
        "        is_trading_day = True\n"
        "        continue\n"
        "    if trade_date > today:\n"
        "        next_trade = trade_date\n"
        "        break\n"
        "print(json.dumps({\n"
        "    'calendarDate': today.isoformat(),\n"
        "    'isTradingDay': is_trading_day,\n"
        "    'previousTradeDate': previous_trade.isoformat() if previous_trade else '',\n"
        "    'nextTradeDate': next_trade.isoformat() if next_trade else ''\n"
        "}, ensure_ascii=True))\n");

    QProcess process;
    process.setWorkingDirectory(repoRoot);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString existingPythonPath = environment.value(QStringLiteral("PYTHONPATH"));
    environment.insert(
        QStringLiteral("PYTHONPATH"),
        existingPythonPath.isEmpty()
            ? repoRoot
            : (repoRoot + QDir::listSeparator() + existingPythonPath));
    process.setProcessEnvironment(environment);

    process.start(resolvePythonExecutable(repoRoot), {QStringLiteral("-c"), script});
    if (!process.waitForStarted(2000)) {
        if (error) {
            *error = QStringLiteral("无法启动 Python 交易日历进程");
        }
        return {};
    }

    if (!process.waitForFinished(12000)) {
        process.kill();
        process.waitForFinished(1000);
        if (error) {
            *error = QStringLiteral("交易日历查询超时");
        }
        return {};
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            const QString standardError = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
            *error = standardError.isEmpty()
                ? QStringLiteral("Python 交易日历进程退出失败，code=%1").arg(process.exitCode())
                : standardError;
        }
        return {};
    }

    return parseCalendarBase(process.readAllStandardOutput(), error);
}

QVariantMap buildFallbackCalendarBase(const QDate& today, const QString& error)
{
    const bool isTradingDay = today.dayOfWeek() >= 1 && today.dayOfWeek() <= 5;
    QString previousTradeDate = queryPreviousKnownTradeDate(today);
    if (previousTradeDate.isEmpty()) {
        previousTradeDate = previousWeekday(today);
    }

    QVariantMap result;
    result.insert(QStringLiteral("calendarDate"), today.toString(QStringLiteral("yyyy-MM-dd")));
    result.insert(QStringLiteral("isTradingDay"), isTradingDay);
    result.insert(QStringLiteral("previousTradeDate"), previousTradeDate);
    result.insert(QStringLiteral("nextTradeDate"), nextWeekday(today));
    result.insert(QStringLiteral("source"), QStringLiteral("fallback"));
    result.insert(QStringLiteral("sourceLabel"), sourceLabel(QStringLiteral("fallback")));
    result.insert(QStringLiteral("holidayAware"), false);
    result.insert(QStringLiteral("available"), true);
    if (!error.trimmed().isEmpty()) {
        result.insert(QStringLiteral("error"), error.trimmed());
    }
    return result;
}

QVariantMap buildSessionSnapshot(const QVariantMap& base, const QDateTime& now)
{
    QVariantMap result = base;
    const QDate today = now.date();
    const QString todayText = today.toString(QStringLiteral("yyyy-MM-dd"));
    const bool isTradingDay = result.value(QStringLiteral("isTradingDay")).toBool();
    const QString phase = sessionPhaseCode(isTradingDay, now.time());
    const bool sessionOpen = phase == QStringLiteral("TRADING");

    const QString previousTradeDate = normalizeTradeDateText(result.value(QStringLiteral("previousTradeDate")).toString());
    const QString nextTradeDate = normalizeTradeDateText(result.value(QStringLiteral("nextTradeDate")).toString());

    QString latestClosedTradeDate = previousTradeDate;
    if (isTradingDay && (now.time().hour() * 60 + now.time().minute()) >= kMarketCloseMinutes) {
        latestClosedTradeDate = todayText;
    }

    QString nextOpenDate;
    if (isTradingDay && (phase == QStringLiteral("PRE_OPEN") || phase == QStringLiteral("TRADING") || phase == QStringLiteral("LUNCH_BREAK"))) {
        nextOpenDate = todayText;
    } else {
        nextOpenDate = nextTradeDate;
    }

    result.insert(QStringLiteral("calendarDate"), todayText);
    result.insert(QStringLiteral("timestamp"), now.toString(Qt::ISODate));
    result.insert(QStringLiteral("sessionPhase"), phase);
    result.insert(QStringLiteral("sessionPhaseLabel"), sessionPhaseLabel(phase));
    result.insert(QStringLiteral("sessionOpen"), sessionOpen);
    result.insert(QStringLiteral("latestClosedTradeDate"), latestClosedTradeDate);
    result.insert(QStringLiteral("nextOpenDate"), nextOpenDate);
    result.insert(QStringLiteral("sourceLabel"), sourceLabel(result.value(QStringLiteral("source")).toString()));
    return result;
}

} // namespace

TradingMarketCalendarService* TradingMarketCalendarService::m_instance = nullptr;
QMutex TradingMarketCalendarService::m_instanceMutex;

TradingMarketCalendarService* TradingMarketCalendarService::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        QCoreApplication* app = QCoreApplication::instance();
        if (app && QThread::currentThread() != app->thread()) {
            QMetaObject::invokeMethod(app, [app]() {
                if (!m_instance) {
                    m_instance = new TradingMarketCalendarService(app);
                }
            }, Qt::BlockingQueuedConnection);
        } else {
            m_instance = new TradingMarketCalendarService(app);
        }
    }
    return m_instance;
}

TradingMarketCalendarService::TradingMarketCalendarService(QObject* parent)
    : QObject(parent)
    , m_initialized(false)
    , m_hasTestingSessionSnapshotOverride(false)
{
}

void TradingMarketCalendarService::initialize()
{
    bool needsEmit = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_initialized) {
            m_initialized = true;
            needsEmit = true;
        }
    }

    refresh();

    if (needsEmit) {
        emit initializedChanged();
    }
}

void TradingMarketCalendarService::initializeAsync()
{
    bool needsEmit = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_initialized) {
            m_initialized = true;
            needsEmit = true;
        }
    }

    if (needsEmit) {
        emit initializedChanged();
    }

    refreshAsync();
}

bool TradingMarketCalendarService::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}

QVariantMap TradingMarketCalendarService::currentSessionSnapshot() const
{
    QVariantMap testingOverride;
    QVariantMap currentSnapshot;
    {
        QMutexLocker locker(&m_mutex);
        if (m_hasTestingSessionSnapshotOverride) {
            testingOverride = m_testingSessionSnapshotOverride;
        } else {
            currentSnapshot = m_currentSessionSnapshot;
        }
    }

    if (!testingOverride.isEmpty()) {
        return testingOverride;
    }
    if (!currentSnapshot.isEmpty()) {
        return currentSnapshot;
    }

    return buildSessionSnapshot(
        buildFallbackCalendarBase(QDate::currentDate(), QStringLiteral("market_calendar_not_initialized")),
        QDateTime::currentDateTime());
}

bool TradingMarketCalendarService::isHolidayAware() const
{
    return currentSessionSnapshot().value(QStringLiteral("holidayAware")).toBool();
}

bool TradingMarketCalendarService::isTradingSessionOpen() const
{
    return currentSessionSnapshot().value(QStringLiteral("sessionOpen")).toBool();
}

void TradingMarketCalendarService::setSessionSnapshotOverrideForTesting(const QVariantMap& snapshot)
{
    QVariantMap normalizedSnapshot = snapshot;
    if (!normalizedSnapshot.contains(QStringLiteral("calendarDate"))) {
        normalizedSnapshot.insert(QStringLiteral("calendarDate"), QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
    }
    if (!normalizedSnapshot.contains(QStringLiteral("timestamp"))) {
        normalizedSnapshot.insert(QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(Qt::ISODate));
    }
    if (!normalizedSnapshot.contains(QStringLiteral("source"))) {
        normalizedSnapshot.insert(QStringLiteral("source"), QStringLiteral("testing_override"));
    }
    if (!normalizedSnapshot.contains(QStringLiteral("sourceLabel"))) {
        normalizedSnapshot.insert(QStringLiteral("sourceLabel"), QStringLiteral("Testing Override"));
    }
    if (!normalizedSnapshot.contains(QStringLiteral("available"))) {
        normalizedSnapshot.insert(QStringLiteral("available"), true);
    }

    {
        QMutexLocker locker(&m_mutex);
        m_testingSessionSnapshotOverride = normalizedSnapshot;
        m_hasTestingSessionSnapshotOverride = true;
    }

    emit currentSessionSnapshotChanged();
}

void TradingMarketCalendarService::clearSessionSnapshotOverrideForTesting()
{
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_hasTestingSessionSnapshotOverride) {
            m_testingSessionSnapshotOverride.clear();
            m_hasTestingSessionSnapshotOverride = false;
            changed = true;
        }
    }

    if (changed) {
        emit currentSessionSnapshotChanged();
    }
}

void TradingMarketCalendarService::refresh()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_hasTestingSessionSnapshotOverride) {
            return;
        }
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QString todayText = now.date().toString(QStringLiteral("yyyy-MM-dd"));

    QVariantMap base;
    {
        QMutexLocker locker(&m_mutex);
        base = m_calendarBase;
    }

    if (base.value(QStringLiteral("calendarDate")).toString() != todayText) {
        QString error;
        const QVariantMap pythonBase = loadCalendarBaseFromPython(resolveRepoRoot(), &error);
        base = pythonBase.isEmpty() ? buildFallbackCalendarBase(now.date(), error) : pythonBase;

        QMutexLocker locker(&m_mutex);
        m_calendarBase = base;
    }

    const QVariantMap snapshot = buildSessionSnapshot(base, now);

    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_currentSessionSnapshot != snapshot) {
            m_currentSessionSnapshot = snapshot;
            changed = true;
        }
    }

    if (changed) {
        emit currentSessionSnapshotChanged();
    }
}

void TradingMarketCalendarService::refreshAsync()
{
    bool hasOverride = false;
    QVariantMap base;
    {
        QMutexLocker locker(&m_mutex);
        hasOverride = m_hasTestingSessionSnapshotOverride;
        base = m_calendarBase;
    }

    if (hasOverride) {
        return;
    }

    QPointer<TradingMarketCalendarService> safeService(this);
    std::thread([safeService, base]() mutable {
        const QDateTime now = QDateTime::currentDateTime();
        const QString todayText = now.date().toString(QStringLiteral("yyyy-MM-dd"));
        QVariantMap computedBase = base;

        if (computedBase.value(QStringLiteral("calendarDate")).toString() != todayText) {
            QString error;
            const QVariantMap pythonBase = loadCalendarBaseFromPython(resolveRepoRoot(), &error);
            computedBase = pythonBase.isEmpty() ? buildFallbackCalendarBase(now.date(), error) : pythonBase;
        }

        const QVariantMap snapshot = buildSessionSnapshot(computedBase, now);
        if (!safeService) {
            return;
        }

        QMetaObject::invokeMethod(safeService.data(), [safeService, computedBase, snapshot]() {
            if (!safeService) {
                return;
            }

            bool changed = false;
            {
                QMutexLocker locker(&safeService->m_mutex);
                if (safeService->m_hasTestingSessionSnapshotOverride) {
                    return;
                }
                safeService->m_calendarBase = computedBase;
                if (safeService->m_currentSessionSnapshot != snapshot) {
                    safeService->m_currentSessionSnapshot = snapshot;
                    changed = true;
                }
            }

            if (changed) {
                emit safeService->currentSessionSnapshotChanged();
            }
        }, Qt::QueuedConnection);
    }).detach();
}