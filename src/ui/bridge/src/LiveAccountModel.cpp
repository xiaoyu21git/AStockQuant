#include "LiveAccountModel.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QCoreApplication>

LiveAccountModel::LiveAccountModel(QObject* parent)
    : QObject(parent)
{
}

void LiveAccountModel::refresh()
{
    // 简单实现：默认从应用根目录下的 data/live_account_snapshot.json 读取
    const QString baseDir = QCoreApplication::applicationDirPath() + "/../..";
    const QString path = baseDir + "/data/live_account_snapshot.json";

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        // 打不开文件则保持旧值
        return;
    }

    const QByteArray bytes = file.readAll();
    file.close();

    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject())
        return;

    const QJsonObject obj = doc.object();

    const double totalAsset = obj.value("total_asset").toDouble();
    const double available = obj.value("available").toDouble();
    const double positionMv = obj.value("position_market_value").toDouble();
    const double todayPnl = obj.value("today_pnl").toDouble();

    bool changed = false;

    if (!qFuzzyCompare(1.0 + m_totalAsset, 1.0 + totalAsset)) {
        m_totalAsset = totalAsset;
        changed = true;
    }
    if (!qFuzzyCompare(1.0 + m_availableCash, 1.0 + available)) {
        m_availableCash = available;
        changed = true;
    }
    if (!qFuzzyCompare(1.0 + m_positionMarketValue, 1.0 + positionMv)) {
        m_positionMarketValue = positionMv;
        changed = true;
    }
    if (!qFuzzyCompare(1.0 + m_todayPnl, 1.0 + todayPnl)) {
        m_todayPnl = todayPnl;
        changed = true;
    }

    if (changed)
        emit accountChanged();
}

//#include "moc_LiveAccountModel.cpp"
