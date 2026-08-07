// ConfigVariantAdapter.h — QVariantMap ↔ ConfigNode 转换适配器
// 消除 RiskConfigService 与 TradingConnectionConfigService 中的重复定义
#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>
#include "foundation/config/ConfigManager.hpp"
#include "foundation/json/json_facade.h"

namespace bridge {

/// @brief QVariantMap → ConfigNode (通过 JSON 字符串中转)
inline foundation::config::ConfigNode toConfigNode(const QVariantMap& map) {
    QJsonDocument doc(QJsonObject::fromVariantMap(map));
    auto json = foundation::json::JsonFacade::parse(doc.toJson().toStdString());
    return foundation::config::ConfigNode(json);
}

/// @brief ConfigNode → QVariantMap (通过 JSON 字符串中转)
inline QVariantMap toVariantMap(const foundation::config::ConfigNode& node) {
    if (node.isNull()) return {};
    auto doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(node.toJsonString()));
    if (!doc.isObject()) return {};
    return doc.object().toVariantMap();
}

} // namespace bridge
