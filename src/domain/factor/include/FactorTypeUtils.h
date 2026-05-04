#pragma once

#include <QString>

namespace factor {

inline QString normalizeFactorTypeId(const QString& rawType)
{
    const QString normalized = rawType.trimmed().toLower();
    if (normalized == QStringLiteral("value") || normalized == QString::fromUtf8("价值因子")) {
        return QStringLiteral("value");
    }
    if (normalized == QStringLiteral("momentum") || normalized == QString::fromUtf8("动量因子")) {
        return QStringLiteral("momentum");
    }
    if (normalized == QStringLiteral("size") || normalized == QString::fromUtf8("规模因子")) {
        return QStringLiteral("size");
    }
    if (normalized == QStringLiteral("quality") || normalized == QString::fromUtf8("质量因子")) {
        return QStringLiteral("quality");
    }
    if (normalized == QStringLiteral("growth") || normalized == QString::fromUtf8("成长因子")) {
        return QStringLiteral("growth");
    }
    if (normalized == QStringLiteral("dividend") || normalized == QString::fromUtf8("红利因子")) {
        return QStringLiteral("dividend");
    }
    if (normalized == QStringLiteral("technical") || normalized == QString::fromUtf8("技术因子")) {
        return QStringLiteral("technical");
    }
    if (normalized == QStringLiteral("liquidity") || normalized == QString::fromUtf8("流动性因子")) {
        return QStringLiteral("liquidity");
    }
    if (normalized == QStringLiteral("macro") || normalized == QString::fromUtf8("宏观因子")) {
        return QStringLiteral("macro");
    }
    if (normalized == QStringLiteral("industry") || normalized == QString::fromUtf8("行业因子")) {
        return QStringLiteral("industry");
    }
    if (normalized == QStringLiteral("sentiment") || normalized == QString::fromUtf8("情绪因子")) {
        return QStringLiteral("sentiment");
    }
    if (normalized == QStringLiteral("custom")
            || normalized == QString::fromUtf8("自定义因子")
            || normalized == QString::fromUtf8("自定义")) {
        return QStringLiteral("custom");
    }
    if (normalized == QStringLiteral("low_volatility")
            || normalized == QString::fromUtf8("低波因子")
            || normalized == QString::fromUtf8("低波动因子")) {
        return QStringLiteral("low_volatility");
    }
    return normalized;
}

inline QString canonicalFactorDisplayName(const QString& rawType)
{
    const QString normalizedType = normalizeFactorTypeId(rawType);
    if (normalizedType == QStringLiteral("value")) {
        return QString::fromUtf8("价值因子");
    }
    if (normalizedType == QStringLiteral("momentum")) {
        return QString::fromUtf8("动量因子");
    }
    if (normalizedType == QStringLiteral("size")) {
        return QString::fromUtf8("规模因子");
    }
    if (normalizedType == QStringLiteral("quality")) {
        return QString::fromUtf8("质量因子");
    }
    if (normalizedType == QStringLiteral("growth")) {
        return QString::fromUtf8("成长因子");
    }
    if (normalizedType == QStringLiteral("dividend")) {
        return QString::fromUtf8("红利因子");
    }
    if (normalizedType == QStringLiteral("technical")) {
        return QString::fromUtf8("技术因子");
    }
    if (normalizedType == QStringLiteral("liquidity")) {
        return QString::fromUtf8("流动性因子");
    }
    if (normalizedType == QStringLiteral("macro")) {
        return QString::fromUtf8("宏观因子");
    }
    if (normalizedType == QStringLiteral("industry")) {
        return QString::fromUtf8("行业因子");
    }
    if (normalizedType == QStringLiteral("sentiment")) {
        return QString::fromUtf8("情绪因子");
    }
    if (normalizedType == QStringLiteral("custom")) {
        return QString::fromUtf8("自定义因子");
    }
    if (normalizedType == QStringLiteral("low_volatility")) {
        return QString::fromUtf8("低波因子");
    }
    return {};
}

}  // namespace factor