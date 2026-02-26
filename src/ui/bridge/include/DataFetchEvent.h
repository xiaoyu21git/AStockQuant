// DataFetchEvent.h - 数据获取事件定义
#pragma once

#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

#include "Event/EventFormat.hpp"

namespace DataFetchEvents {

// 事件类型定义
constexpr const char* EVENT_DATA_FETCH_REQUEST = "data_fetch_request";
constexpr const char* EVENT_DATA_FETCH_RESPONSE = "data_fetch_response";
constexpr const char* EVENT_DATA_FETCH_ERROR = "data_fetch_error";

// 创建数据获取请求事件
inline engine::EventFormat createDataFetchRequestEvent(
    const QString& requestId,
    const QString& symbol,
    const QString& startDate,
    const QString& endDate,
    const QString& source = "database_fallback"
) {
    // 使用工厂方法创建EventFormat
    auto event = engine::EventFormat::create_from_strings(
        EVENT_DATA_FETCH_REQUEST, 
        source.toStdString()
    );
    
    // 设置事件数据
    event.set("request_id", requestId.toStdString());
    event.set("symbol", symbol.toStdString());
    event.set("start_date", startDate.toStdString());
    event.set("end_date", endDate.toStdString());
    event.set("timestamp", QDateTime::currentDateTime().toString(Qt::ISODate).toStdString());
    
    return event;
}

// 创建数据获取响应事件
inline engine::EventFormat createDataFetchResponseEvent(
    const QString& requestId,
    const QJsonArray& data,
    int dataCount,
    const QString& status = "success",
    const QString& message = ""
) {
    // 使用工厂方法创建EventFormat
    auto event = engine::EventFormat::create_from_strings(
        EVENT_DATA_FETCH_RESPONSE,
        "juejin_data_fetcher"
    );
    
    // 将QJsonArray转换为JSON字符串以便存储
    QJsonObject response;
    response["request_id"] = requestId;
    response["status"] = status;
    response["message"] = message;
    response["data_count"] = dataCount;
    response["data"] = data;
    response["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    QJsonDocument doc(response);
    
    // 存储JSON字符串到事件数据中
    event.set("json_data", doc.toJson(QJsonDocument::Compact).toStdString());
    event.set("request_id", requestId.toStdString());
    event.set("status", status.toStdString());
    event.set("message", message.toStdString());
    event.set("data_count", static_cast<int64_t>(dataCount));
    
    return event;
}

// 创建数据获取错误事件
inline engine::EventFormat createDataFetchErrorEvent(
    const QString& requestId,
    const QString& errorMessage,
    const QString& source = "juejin_data_fetcher"
) {
    // 使用工厂方法创建EventFormat
    auto event = engine::EventFormat::create_from_strings(
        EVENT_DATA_FETCH_ERROR,
        source.toStdString()
    );
    
    // 设置错误数据
    event.set("request_id", requestId.toStdString());
    event.set("error_message", errorMessage.toStdString());
    event.set("timestamp", QDateTime::currentDateTime().toString(Qt::ISODate).toStdString());
    
    return event;
}

// 解析数据获取请求事件
inline bool parseDataFetchRequestEvent(
    const engine::EventFormat& event,
    QString& requestId,
    QString& symbol,
    QString& startDate,
    QString& endDate
) {
    if (event.type != EVENT_DATA_FETCH_REQUEST) {
        return false;
    }
    
    try {
        auto reqIdOpt = event.get<std::string>("request_id");
        auto symbolOpt = event.get<std::string>("symbol");
        auto startDateOpt = event.get<std::string>("start_date");
        auto endDateOpt = event.get<std::string>("end_date");
        
        if (!reqIdOpt || !symbolOpt || !startDateOpt || !endDateOpt) {
            return false;
        }
        
        requestId = QString::fromStdString(*reqIdOpt);
        symbol = QString::fromStdString(*symbolOpt);
        startDate = QString::fromStdString(*startDateOpt);
        endDate = QString::fromStdString(*endDateOpt);
        
        return !requestId.isEmpty() && !startDate.isEmpty() && !endDate.isEmpty();
    } catch (...) {
        return false;
    }
}

// 解析数据获取响应事件
inline bool parseDataFetchResponseEvent(
    const engine::EventFormat& event,
    QString& requestId,
    QJsonArray& data,
    int& dataCount,
    QString& status,
    QString& message
) {
    if (event.type != EVENT_DATA_FETCH_RESPONSE) {
        return false;
    }
    
    try {
        auto jsonDataOpt = event.get<std::string>("json_data");
        if (!jsonDataOpt) {
            return false;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(*jsonDataOpt));
        if (doc.isNull() || !doc.isObject()) {
            return false;
        }
        
        QJsonObject response = doc.object();
        requestId = response.value("request_id").toString();
        status = response.value("status").toString("success");
        message = response.value("message").toString();
        dataCount = response.value("data_count").toInt(0);
        data = response.value("data").toArray();
        
        return !requestId.isEmpty();
    } catch (...) {
        return false;
    }
}

// 解析数据获取错误事件
inline bool parseDataFetchErrorEvent(
    const engine::EventFormat& event,
    QString& requestId,
    QString& errorMessage
) {
    if (event.type != EVENT_DATA_FETCH_ERROR) {
        return false;
    }
    
    try {
        auto reqIdOpt = event.get<std::string>("request_id");
        auto errorMsgOpt = event.get<std::string>("error_message");
        
        if (!reqIdOpt || !errorMsgOpt) {
            return false;
        }
        
        requestId = QString::fromStdString(*reqIdOpt);
        errorMessage = QString::fromStdString(*errorMsgOpt);
        
        return !requestId.isEmpty() && !errorMessage.isEmpty();
    } catch (...) {
        return false;
    }
}

// 辅助函数：生成请求ID
inline QString generateRequestId(const QString& prefix = "juejin_fetch") {
    return prefix + "_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// 辅助函数：将QVariantList转换为QJsonArray
inline QJsonArray variantListToJsonArray(const QVariantList& data) {
    QJsonArray jsonArray;
    for (const QVariant& item : data) {
        if (item.type() == QVariant::Map) {
            QVariantMap map = item.toMap();
            QJsonObject obj;
            for (auto it = map.begin(); it != map.end(); ++it) {
                obj[it.key()] = QJsonValue::fromVariant(it.value());
            }
            jsonArray.append(obj);
        } else if (item.canConvert<QJsonValue>()) {
            jsonArray.append(QJsonValue::fromVariant(item));
        }
    }
    return jsonArray;
}

// 辅助函数：将QJsonArray转换为QVariantList
inline QVariantList jsonArrayToVariantList(const QJsonArray& jsonArray) {
    QVariantList data;
    for (const QJsonValue& value : jsonArray) {
        if (value.isObject()) {
            QVariantMap map;
            QJsonObject obj = value.toObject();
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                map[it.key()] = it.value().toVariant();
            }
            data.append(map);
        } else {
            data.append(value.toVariant());
        }
    }
    return data;
}

} // namespace DataFetchEvents