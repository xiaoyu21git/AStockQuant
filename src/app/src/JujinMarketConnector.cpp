#include "JujinMarketConnector.h"

#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QDebug>
#include <QTemporaryFile>

#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"
#include "JujinApi.h"

namespace {

std::string trim(const std::string& value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }

    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string toGmMarketSymbol(std::string symbol)
{
    symbol = trim(symbol);
    if (symbol.empty()) {
        return symbol;
    }

    std::transform(symbol.begin(), symbol.end(), symbol.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    if (symbol.rfind("SHSE.", 0) == 0 || symbol.rfind("SZSE.", 0) == 0 || symbol.rfind("BSE.", 0) == 0
        || symbol.rfind("CFFEX.", 0) == 0 || symbol.rfind("SHFE.", 0) == 0 || symbol.rfind("DCE.", 0) == 0
        || symbol.rfind("CZCE.", 0) == 0 || symbol.rfind("INE.", 0) == 0 || symbol.rfind("GFEX.", 0) == 0) {
        return symbol;
    }

    const auto dot = symbol.find('.');
    if (dot == std::string::npos) {
        return symbol;
    }

    const std::string code = symbol.substr(0, dot);
    const std::string exchange = symbol.substr(dot + 1);
    if (exchange == "SH") {
        return "SHSE." + code;
    }
    if (exchange == "SZ") {
        return "SZSE." + code;
    }
    if (exchange == "BJ") {
        return "BSE." + code;
    }
    if (exchange == "CFFEX" || exchange == "SHFE" || exchange == "DCE"
        || exchange == "CZCE" || exchange == "INE" || exchange == "GFEX") {
        return exchange + "." + code;
    }
    return symbol;
}

QString connectorConfigFilePath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("config/trading_connection.json"));
}

QJsonObject readConnectorConfigObject()
{
    QFile file(connectorConfigFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    return document.isObject() ? document.object() : QJsonObject();
}

std::string readStringSetting(const QJsonObject& configObject, const char* key, const char* envName, const char* fallback = "")
{
    const QString configValue = configObject.value(QString::fromUtf8(key)).toString().trimmed();
    if (!configValue.isEmpty()) {
        return configValue.toStdString();
    }

    if (const char* envValue = std::getenv(envName)) {
        return envValue;
    }

    return fallback ? std::string(fallback) : std::string();
}

bool readBoolSetting(const QJsonObject& configObject, const char* key, const char* envName)
{
    const QJsonValue configValue = configObject.value(QString::fromUtf8(key));
    if (!configValue.isUndefined()) {
        if (configValue.isBool()) {
            return configValue.toBool();
        }

        const QString stringValue = configValue.toString().trimmed().toLower();
        return stringValue == QStringLiteral("1") || stringValue == QStringLiteral("true");
    }

    if (const char* envValue = std::getenv(envName)) {
        const std::string value = envValue;
        return value == "1" || value == "true" || value == "TRUE";
    }

    return false;
}

QStringList readClientProcessNames(const QJsonObject& configObject)
{
    const QVariant value = configObject.value(QStringLiteral("clientProcessNames")).toVariant();
    if (value.canConvert<QStringList>()) {
        return value.toStringList();
    }

    const QString text = configObject.value(QStringLiteral("clientProcessNames")).toString();
    if (text.trimmed().isEmpty()) {
        return {
            QStringLiteral("myquant.exe"),
            QStringLiteral("MiniQmt.exe"),
            QStringLiteral("XtMiniQmt.exe"),
            QStringLiteral("gmtrade.exe"),
            QStringLiteral("闂佸湱鍎ら敃銏ゅ闯妤ｅ啯鐓傞煫鍥ㄦ尭椤曆呯磽娴ｅ摜澧㈡い?exe"),
            QStringLiteral("ds-proxy.exe"),
            QStringLiteral("gmterm-serv.exe"),
            QStringLiteral("闂佹悶鍎存慨銈嗘叏閸モ晜瀚氬ù锝囶焾閻撴牠鏌熼悜姗堣€块柛?exe")
        };
    }

    QStringList names;
    const QStringList rawNames = text.split(',', Qt::SkipEmptyParts);
    for (const QString& rawName : rawNames) {
        const QString trimmedName = rawName.trimmed();
        if (!trimmedName.isEmpty()) {
            names.append(trimmedName);
        }
    }
    return names;
}

bool hasRequiredClientProcess(const QJsonObject& configObject, QString* matchedProcessName = nullptr)
{
    const QStringList processNames = readClientProcessNames(configObject);
    if (processNames.isEmpty()) {
        if (matchedProcessName) {
            matchedProcessName->clear();
        }
        return false;
    }

    QProcess process;
    process.start(QStringLiteral("tasklist"), {QStringLiteral("/FO"), QStringLiteral("CSV"), QStringLiteral("/NH")});
    if (!process.waitForFinished(3000) || process.exitStatus() != QProcess::NormalExit) {
        if (matchedProcessName) {
            matchedProcessName->clear();
        }
        return false;
    }

    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput()).toLower();
    for (const QString& processName : processNames) {
        const QString candidate = processName.trimmed().toLower();
        if (!candidate.isEmpty() && output.contains(QStringLiteral("\"") + candidate + QStringLiteral("\""))) {
            if (matchedProcessName) {
                *matchedProcessName = processName.trimmed();
            }
            return true;
        }
    }

    if (matchedProcessName) {
        matchedProcessName->clear();
    }
    return false;
}

int64_t toEpochUs(const std::chrono::system_clock::time_point& timePoint)
{
    if (timePoint == std::chrono::system_clock::time_point()) {
        return 0;
    }

    return std::chrono::duration_cast<std::chrono::microseconds>(
        timePoint.time_since_epoch()).count();
}

QString formatOrderTime(const std::chrono::system_clock::time_point& timePoint)
{
    if (timePoint == std::chrono::system_clock::time_point()) {
        return {};
    }

    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        timePoint.time_since_epoch()).count();
    return QDateTime::fromMSecsSinceEpoch(milliseconds, Qt::LocalTime)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
}

std::string orderSideToString(thirdparty::OrderSide side)
{
    return side == thirdparty::OrderSide::SELL ? "SELL" : "BUY";
}

QString jsonStringValue(const QJsonObject& object, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const QJsonValue value = object.value(QString::fromUtf8(key));
        if (value.isString()) {
            const QString text = value.toString().trimmed();
            if (!text.isEmpty()) {
                return text;
            }
        }
        if (value.isDouble()) {
            return QString::number(value.toDouble(), 'f', 6);
        }
        if (value.isBool()) {
            return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        }
    }
    return {};
}

double jsonDoubleValue(const QJsonObject& object, std::initializer_list<const char*> keys, double fallback = 0.0)
{
    for (const char* key : keys) {
        const QJsonValue value = object.value(QString::fromUtf8(key));
        if (value.isDouble()) {
            return value.toDouble();
        }
        if (value.isString()) {
            bool ok = false;
            const double parsed = value.toString().trimmed().toDouble(&ok);
            if (ok) {
                return parsed;
            }
        }
    }
    return fallback;
}

QString normalizeOrderSide(const QString& side)
{
    const QString normalized = side.trimmed().toUpper();
    if (normalized == QStringLiteral("1") || normalized == QStringLiteral("BUY") || normalized == QStringLiteral("LONG")) {
        return QStringLiteral("BUY");
    }
    if (normalized == QStringLiteral("2") || normalized == QStringLiteral("SELL") || normalized == QStringLiteral("SHORT")) {
        return QStringLiteral("SELL");
    }
    return normalized;
}

QString normalizeOrderStatus(const QString& status)
{
    const QString normalized = status.trimmed().toUpper();
    if (normalized.isEmpty()) {
        return QStringLiteral("SUBMITTED");
    }
    if (normalized == QStringLiteral("0") || normalized == QStringLiteral("UNKNOWN")) {
        return QStringLiteral("PENDING");
    }
    if (normalized == QStringLiteral("1") || normalized == QStringLiteral("10") || normalized == QStringLiteral("13")) {
        return QStringLiteral("SUBMITTED");
    }
    if (normalized == QStringLiteral("2")) {
        return QStringLiteral("PARTIAL_FILLED");
    }
    if (normalized == QStringLiteral("3")) {
        return QStringLiteral("FILLED");
    }
    if (normalized == QStringLiteral("4") || normalized == QStringLiteral("5") || normalized == QStringLiteral("12")) {
        return QStringLiteral("CANCELLED");
    }
    if (normalized == QStringLiteral("8")) {
        return QStringLiteral("REJECTED");
    }
    if (normalized == QStringLiteral("6") || normalized == QStringLiteral("7") || normalized == QStringLiteral("9") || normalized == QStringLiteral("11") || normalized == QStringLiteral("14")) {
        return QStringLiteral("PENDING");
    }
    if (normalized == QStringLiteral("PENDINGNEW") || normalized == QStringLiteral("NEW")) {
        return QStringLiteral("SUBMITTED");
    }
    return normalized;
}

} // namespace

JujinMarketConnector::JujinMarketConnector() = default;

JujinMarketConnector::~JujinMarketConnector()
{
    stop();
}

bool JujinMarketConnector::isEnabledByEnvironment() const
{
    return readBoolSetting(readConnectorConfigObject(), "enabled", "ASTOCK_ENABLE_JUJIN_MARKET");
}

bool JujinMarketConnector::start()
{
    if (m_started) {
        std::cout << "[JujinMarketConnector] start skipped: already started\n";
        return true;
    }

    auto* eventBus = engine::get_engine_event_bus();
    if (!eventBus) {
        m_lastError = "engine event bus is not initialized";
        return false;
    }

    if (thirdparty::JujinApi* sharedApi = engine::get_shared_jujin_api()) {
        if (sharedApi != m_api.get()) {
            m_lastError = "shared jujin trading session already exists";
            return false;
        }
    }

    const QJsonObject configObject = readConnectorConfigObject();
    std::cout << "[JujinMarketConnector] start requested\n";

    QString matchedProcessName;
    const bool hasClientProcess = hasRequiredClientProcess(configObject, &matchedProcessName);
    if (!hasClientProcess) {
        qWarning() << "JujinMarketConnector: configured client process not detected, will still try token-based connection";
    }

    const std::string token = readStringSetting(configObject, "token", "ASTOCK_GM_TOKEN");
    if (token.empty()) {
        m_lastError = "jujin token is empty";
        return false;
    }

    thirdparty::ConfigParams config;
    config.platform = thirdparty::PlatformType::JUJIN;
    config.token = token;
    config.account_id = readStringSetting(configObject, "accountId", "ASTOCK_GM_ACCOUNT_ID");
    config.server_url = readStringSetting(configObject, "serverUrl", "ASTOCK_GM_SERVER_URL");
    config.extra_params["strategy_id"] = readStringSetting(configObject, "strategyId", "ASTOCK_GM_STRATEGY_ID", "astock_quant_ui");
    config.extra_params["mode"] = readStringSetting(configObject, "mode", "ASTOCK_GM_MODE", "1");
    config.extra_params["simtrade_only"] = readBoolSetting(configObject, "simtradeOnly", "ASTOCK_GM_SIMTRADE_ONLY") ? "true" : "false";
    config.extra_params["read_only"] = readBoolSetting(configObject, "readOnly", "ASTOCK_GM_READ_ONLY") ? "true" : "false";

    std::cout << "[JujinMarketConnector] matched process=" << (hasClientProcess ? matchedProcessName.toStdString() : std::string("<not-found>"))
              << " accountId=" << config.account_id
              << " mode=" << config.extra_params["mode"]
              << " simtradeOnly=" << config.extra_params["simtrade_only"]
              << " readOnly=" << config.extra_params["read_only"]
              << " symbols=" << readStringSetting(configObject, "symbols", "ASTOCK_GM_SYMBOLS", "")
              << "\n";

    m_api = std::make_unique<thirdparty::JujinApi>();
    m_api->set_event_bus(std::shared_ptr<engine::EventBus>(eventBus, [](engine::EventBus*) {}));

    if (!m_api->initialize(config)) {
        m_lastError = "initialize failed";
        m_api.reset();
        return false;
    }

    if (!m_api->connect()) {
        m_lastError = "connect failed";
        m_api.reset();
        return false;
    }

    engine::register_shared_jujin_api(m_api.get());

    std::cout << "[JujinMarketConnector] API connected successfully\n";

    const std::vector<std::string> watchlist = watchlistFromEnvironment();
    if (!watchlist.empty()) {
        for (const std::string& symbol : watchlist) {
            if (!subscribeSymbol(symbol, eventBus)) {
                m_lastError = "market subscription setup failed";
                if (engine::get_shared_jujin_api() == m_api.get()) {
                    engine::register_shared_jujin_api(nullptr);
                }
                m_api->disconnect();
                m_api.reset();
                return false;
            }
        }
    }

    m_watchRequestSubscription = eventBus->subscribe("market.watch.ensure",
        [this](const engine::EventFormat& event) {
            auto* bus = engine::get_engine_event_bus();
            if (!m_api || !bus || !bus->is_running()) {
                return;
            }

            auto symbolValue = event.get<std::string>("symbol");
            if (!symbolValue.has_value() || symbolValue->empty()) {
                return;
            }

            if (!subscribeSymbol(*symbolValue, bus)) {
                qWarning() << "JujinMarketConnector: failed to subscribe watch symbol" << QString::fromStdString(*symbolValue);
            }
        });

    std::cout << "[JujinMarketConnector] subscriptions initialized, watchlist size=" << watchlist.size() << "\n";

    m_started = true;
    m_stopRequested.store(false);
    m_lastError.clear();

    publishExistingOrders(eventBus, token, config.account_id);
    std::cout << "[JujinMarketConnector] start completed\n";
    return true;
}

void JujinMarketConnector::stop()
{
    m_stopRequested.store(true);

    if (m_initialOrderSyncThread.joinable()) {
        std::cout << "[JujinMarketConnector] waiting for initial order sync thread\n";
        m_initialOrderSyncThread.join();
    }

    if (!m_api) {
        m_started = false;
        std::cout << "[JujinMarketConnector] stop skipped: api not initialized\n";
        return;
    }

    std::cout << "[JujinMarketConnector] stopping\n";
    {
        std::lock_guard<std::mutex> lock(m_subscriptionMutex);
        m_subscribedSymbols.clear();
    }

    if (engine::get_shared_jujin_api() == m_api.get()) {
        engine::register_shared_jujin_api(nullptr);
    }

    m_api->disconnect();
    m_api.reset();
    m_started = false;
    std::cout << "[JujinMarketConnector] stopped\n";
}

bool JujinMarketConnector::subscribeSymbol(const std::string& symbol, engine::EventBus* eventBus)
{
    if (!m_api || !eventBus || !eventBus->is_running()) {
        return false;
    }

    const std::string normalizedSymbol = toGmMarketSymbol(symbol);
    if (normalizedSymbol.empty()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_subscriptionMutex);
        if (m_subscribedSymbols.find(normalizedSymbol) != m_subscribedSymbols.end()) {
            return true;
        }
    }

    qDebug() << "JujinMarketConnector: subscribe market symbol" << QString::fromStdString(symbol)
             << "->" << QString::fromStdString(normalizedSymbol);

    const std::vector<std::string> symbols{normalizedSymbol};
    if (!m_api->subscribe_market_data(symbols, thirdparty::MarketDataType::TICK, {})) {
        return false;
    }
    if (!m_api->subscribe_market_data(symbols, thirdparty::MarketDataType::BAR_1M, {})) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_subscriptionMutex);
        m_subscribedSymbols.insert(normalizedSymbol);
    }

    return true;
}

const std::string& JujinMarketConnector::lastError() const
{
    return m_lastError;
}

void JujinMarketConnector::publishExistingOrders(engine::EventBus* eventBus,
                                                const std::string& token,
                                                const std::string& accountId)
{
    if (!eventBus || !eventBus->is_running() || token.empty()) {
        return;
    }

    if (m_initialOrderSyncThread.joinable()) {
        m_initialOrderSyncThread.join();
    }

    auto* rawEventBus = eventBus;
    const std::string requestToken = token;
    const std::string requestAccountId = accountId;

    m_initialOrderSyncThread = std::thread([this, rawEventBus, requestToken, requestAccountId]() {
        std::cout << "[JujinMarketConnector] initial unfinished-order sync started asynchronously\n";
        qDebug() << "JujinMarketConnector: initial unfinished-order sync started asynchronously";

        QTemporaryFile scriptFile(QDir::temp().filePath(QStringLiteral("astock_jujin_sync_XXXXXX.py")));
        if (!scriptFile.open()) {
            qWarning() << "JujinMarketConnector: failed to create temp python script file";
            return;
        }

        const QString scriptText = QStringLiteral(
            "import json,sys\n"
            "import gm.api as gm\n"
            "token=sys.argv[1]\n"
            "account_id=sys.argv[2]\n"
            "gm.set_token(token)\n"
            "if account_id:\n"
            "    gm.set_account_id(account_id)\n"
            "orders=gm.get_unfinished_orders() or []\n"
            "def pick(obj,*keys):\n"
            "    for key in keys:\n"
            "        value=obj.get(key) if isinstance(obj, dict) else None\n"
            "        if value is not None and value != '':\n"
            "            return value\n"
            "    return None\n"
            "result=[]\n"
            "for order in orders:\n"
            "    item=order if isinstance(order, dict) else {}\n"
            "    result.append({\n"
            "        'order_id': pick(item,'cl_ord_id','order_id','orderId'),\n"
            "        'symbol': pick(item,'symbol'),\n"
            "        'side': pick(item,'side','position_side'),\n"
            "        'price': pick(item,'price'),\n"
            "        'quantity': pick(item,'volume','quantity'),\n"
            "        'filled_quantity': pick(item,'filled_volume','filledQuantity'),\n"
            "        'filled_notional': pick(item,'filled_amount','filledNotional'),\n"
            "        'status': pick(item,'status'),\n"
            "        'message': pick(item,'ord_rej_reason_detail','status_msg','message'),\n"
            "        'created_at': str(pick(item,'created_at','createdAt') or ''),\n"
            "        'updated_at': str(pick(item,'updated_at','updatedAt') or '')\n"
            "    })\n"
            "print(json.dumps(result, ensure_ascii=True))\n");
        const QByteArray script = scriptText.toUtf8();

        scriptFile.write(script);
        scriptFile.flush();

        QProcess process;

        process.start(QStringLiteral("python"), {
            scriptFile.fileName(),
            QString::fromStdString(requestToken),
            QString::fromStdString(requestAccountId)
        });

        if (!process.waitForStarted(2000)) {
            std::cout << "[JujinMarketConnector] initial unfinished-order sync failed to start python process\n";
            qWarning() << "JujinMarketConnector: initial unfinished-order sync failed to start python process";
            return;
        }

        if (!process.waitForFinished(5000)) {
            process.kill();
            process.waitForFinished(1000);
            std::cout << "[JujinMarketConnector] initial unfinished-order sync timed out\n";
            qWarning() << "JujinMarketConnector: initial unfinished-order sync timed out";
            return;
        }

        if (m_stopRequested.load()) {
            std::cout << "[JujinMarketConnector] initial unfinished-order sync canceled by stop request\n";
            qDebug() << "JujinMarketConnector: initial unfinished-order sync canceled by stop request";
            return;
        }

        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            std::cout << "[JujinMarketConnector] initial unfinished-order sync python exited with code="
                      << process.exitCode() << " stderr="
                      << process.readAllStandardError().toStdString() << "\n";
            qWarning() << "JujinMarketConnector: initial unfinished-order sync python exited with code="
                       << process.exitCode();
            return;
        }

        const QByteArray standardOutput = process.readAllStandardOutput();
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(standardOutput, &parseError);
        if (!document.isArray()) {
            std::cout << "[JujinMarketConnector] initial unfinished-order sync returned invalid json\n";
            qWarning() << "JujinMarketConnector: initial unfinished-order sync returned invalid json"
                       << parseError.errorString()
                       << QString::fromUtf8(standardOutput.left(256));
            return;
        }

        const QJsonArray orders = document.array();
        if (orders.isEmpty()) {
            std::cout << "[JujinMarketConnector] initial unfinished-order sync found no orders\n";
            qDebug() << "JujinMarketConnector: initial unfinished-order sync found no orders";
            return;
        }

        std::size_t publishedCount = 0;
        for (const QJsonValue& value : orders) {
            if (m_stopRequested.load() || !rawEventBus || !rawEventBus->is_running() || !value.isObject()) {
                break;
            }

            const QJsonObject order = value.toObject();
            const QString orderId = jsonStringValue(order, {"order_id", "cl_ord_id", "orderId"});
            const QString symbol = jsonStringValue(order, {"symbol"});
            if (orderId.isEmpty() || symbol.isEmpty()) {
                continue;
            }

            engine::EventFormat event = engine::EventFormat::create_from_strings(
                engine::EventTypes::TRADING_ORDER_UPDATED,
                "TRADING_SNAPSHOT",
                toEpochUs(std::chrono::system_clock::now()));
            event.set("order_id", orderId.toStdString());
            event.set("symbol", symbol.toStdString());
            event.set("side", normalizeOrderSide(jsonStringValue(order, {"side", "position_side"})).toStdString());
            event.set("price", jsonDoubleValue(order, {"price"}, 0.0));
            event.set("quantity", static_cast<int64_t>(jsonDoubleValue(order, {"quantity", "volume"}, 0.0)));
            event.set("filled_quantity", static_cast<int64_t>(jsonDoubleValue(order, {"filled_quantity", "filled_volume"}, 0.0)));
            event.set("filled_notional", jsonDoubleValue(order, {"filled_notional", "filled_amount"}, 0.0));
            event.set("status", normalizeOrderStatus(jsonStringValue(order, {"status"})).toStdString());
            event.set("message", jsonStringValue(order, {"message", "status_msg", "ord_rej_reason_detail"}).toStdString());

            const QString createdAt = jsonStringValue(order, {"created_at", "createdAt"});
            const QString updatedAt = jsonStringValue(order, {"updated_at", "updatedAt"});
            if (!createdAt.isEmpty()) {
                event.set("created_at", createdAt.toStdString());
                event.metadata["created_at"] = createdAt.toStdString();
            }
            if (!updatedAt.isEmpty()) {
                event.set("updated_at", updatedAt.toStdString());
                event.metadata["updated_at"] = updatedAt.toStdString();
            }

            event.metadata["order_id"] = orderId.toStdString();
            event.metadata["symbol"] = symbol.toStdString();
            event.metadata["side"] = normalizeOrderSide(jsonStringValue(order, {"side", "position_side"})).toStdString();
            event.metadata["status"] = normalizeOrderStatus(jsonStringValue(order, {"status"})).toStdString();
            event.metadata["source"] = "snapshot.async";
            event.metadata["event_contract"] = "canonical";

            const auto result = rawEventBus->publish(event, static_cast<int>(engine::EventPriority::HIGH));
            if (!result) {
                std::cout << "[JujinMarketConnector] failed to publish async unfinished-order snapshot: "
                          << result.message << "\n";
                continue;
            }

            ++publishedCount;
        }

        std::cout << "[JujinMarketConnector] initial unfinished-order sync published=" << publishedCount << "\n";
        qDebug() << "JujinMarketConnector: initial unfinished-order sync published=" << static_cast<qulonglong>(publishedCount);
    });
}

std::vector<std::string> JujinMarketConnector::watchlistFromEnvironment() const
{
    const std::string raw = readStringSetting(
        readConnectorConfigObject(),
        "symbols",
        "ASTOCK_GM_SYMBOLS",
        "600000.SH,000001.SZ,600519.SH,300750.SZ");

    std::vector<std::string> symbols;
    std::stringstream stream(raw);
    std::string token;
    while (std::getline(stream, token, ',')) {
        token = trim(token);
        if (!token.empty()) {
            symbols.push_back(token);
        }
    }

    return symbols;
}

std::string JujinMarketConnector::readEnvironment(const char* name, const char* fallback) const
{
    if (const char* value = std::getenv(name)) {
        return value;
    }

    return fallback ? std::string(fallback) : std::string();
}



