#include "JujinApi.h"

#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"
#include "TradingRuntimeManager.h"

#include <QDateTime>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <sstream>

namespace {

std::string runtime_strategy_id_from_config(const thirdparty::ConfigParams& config)
{
    const auto runtime_it = config.extra_params.find("runtime_strategy_id");
    if (runtime_it != config.extra_params.end() && !runtime_it->second.empty()) {
        return runtime_it->second;
    }

    const auto it = config.extra_params.find("strategy_id");
    return it == config.extra_params.end() ? std::string() : it->second;
}

std::string business_strategy_id_from_config(const thirdparty::ConfigParams& config)
{
    const auto it = config.extra_params.find("bound_strategy_id");
    return it == config.extra_params.end() ? std::string() : it->second;
}

std::string display_strategy_id_from_config(const thirdparty::ConfigParams& config)
{
    const std::string business_strategy_id = business_strategy_id_from_config(config);
    return business_strategy_id.empty() ? runtime_strategy_id_from_config(config) : business_strategy_id;
}

std::string build_pending_confirmation_order_id(const std::string& order_id)
{
    return std::string("pending-confirmation-") + order_id;
}

bool should_queue_order_until_runtime_ready(const thirdparty::OrderResult& order)
{
    if (order.order_id.empty()) {
        return true;
    }

    const QString status = QString::fromStdString(order.status).trimmed().toUpper();
    const QString message = QString::fromStdString(order.message).trimmed().toLower();
    return status == QStringLiteral("REJECTED")
        && (message.isEmpty()
            || message.contains(QStringLiteral("trading runtime is not ready"))
            || message.contains(QStringLiteral("timed out waiting for sdk callback thread to place order")));
}

} // namespace

namespace thirdparty {

class JujinApi::Impl {
public:
    Impl()
        : initialized_(false)
        , connected_(false)
        , event_bus_(nullptr)
        , last_error_("")
    {
    }

    ~Impl()
    {
        disconnect();
    }

    bool initialize(const ConfigParams& config)
    {
        QMutexLocker locker(&mutex_);

        if (initialized_) {
            return true;
        }

        config_ = config;

        if (config_.token.empty()) {
            last_error_ = "掘金token不能为空";
            return false;
        }

        if (config_.account_id.empty()) {
            last_error_ = "掘金account_id不能为空";
            return false;
        }

        if (config_.platform != PlatformType::JUJIN && config_.platform != PlatformType::SIMULATION) {
            last_error_ = "不支持的平台类型";
            return false;
        }

        if (!ensure_runtime_session_locked()) {
            last_error_ = "无法创建交易运行时会话";
            return false;
        }

        initialized_ = true;
        last_error_.clear();
        return true;
    }

    bool connect()
    {
        QMutexLocker locker(&mutex_);

        if (!initialized_) {
            last_error_ = "API未初始化";
            return false;
        }

        if (connected_) {
            return true;
        }

        if (!ensure_runtime_session_locked()) {
            last_error_ = "交易运行时会话不可用";
            return false;
        }

        TradingRuntimeManager::instance().set_event_bus(event_bus_);
        if (!TradingRuntimeManager::instance().start_session(config_.account_id)) {
            last_error_ = runtime_session_->last_error_message();
            if (last_error_.empty()) {
                last_error_ = "无法启动交易运行时会话";
            }
            return false;
        }

        connected_ = true;
        publish_connection_event_locked("broker.connected");
        qDebug() << "JujinApi: runtime session started" << QString::fromStdString(config_.account_id);
        return true;
    }

    bool disconnect()
    {
        QMutexLocker locker(&mutex_);

        if (!connected_) {
            return true;
        }

        if (!config_.account_id.empty()) {
            TradingRuntimeManager::instance().stop_session(config_.account_id);
        }

        connected_ = false;
        publish_connection_event_locked("broker.disconnected");
        qDebug() << "JujinApi: runtime session stopped" << QString::fromStdString(config_.account_id);
        return true;
    }

    bool is_connected() const
    {
        QMutexLocker locker(&mutex_);
        return connected_ && runtime_session_;
    }

    bool is_initialized() const
    {
        QMutexLocker locker(&mutex_);
        return initialized_;
    }

    std::string place_order(const std::string& symbol,
                            OrderSide side,
                            OrderType type,
                            double price,
                            double quantity,
                            const std::string& client_order_id,
                            const std::map<std::string, std::string>& metadata)
    {
        QMutexLocker locker(&mutex_);

        if (!connected_) {
            last_error_ = "未连接到券商";
            return "";
        }

        const auto actionIt = metadata.find("action");
        const bool isOptionExercise = actionIt != metadata.end()
            && (actionIt->second == "optionExercise" || actionIt->second == "exercise" || actionIt->second == "option_exercise");
        const bool isCashRepay = actionIt != metadata.end()
            && (actionIt->second == "repay" || actionIt->second == "cashRepay" || actionIt->second == "creditRepayCash");
        const bool isShareReturn = actionIt != metadata.end()
            && (actionIt->second == "returnStock" || actionIt->second == "repayShare" || actionIt->second == "creditRepayShare");
        const bool requiresPrice = !isOptionExercise && !isCashRepay && !isShareReturn;
        const bool requiresQuantity = !isCashRepay;
        double cashAmount = 0.0;
        const auto cashAmountIt = metadata.find("cashAmount");
        if (cashAmountIt != metadata.end()) {
            bool ok = false;
            const double parsed = QString::fromStdString(cashAmountIt->second).toDouble(&ok);
            if (ok) {
                cashAmount = parsed;
            }
        }

        if ((symbol.empty() && !isCashRepay)
            || (requiresPrice && price <= 0.0)
            || (requiresQuantity && quantity <= 0.0)
            || (isCashRepay && cashAmount <= 0.0)) {
            last_error_ = "无效的订单参数";
            return "";
        }

        if (!ensure_runtime_session_locked()) {
            last_error_ = "交易运行时会话未启动";
            return "";
        }

        const std::string order_id = client_order_id.empty() ? generate_order_id() : client_order_id;

        TradingCommand command;
        command.type = TradingCommandType::PlaceOrder;
        command.account_id = config_.account_id;
        command.strategy_id = runtime_strategy_id_from_config(config_);
        command.symbol = symbol;
        command.order_id = order_id;
        command.side = side;
        command.order_type = type;
        command.price = price;
        command.quantity = quantity;
        command.metadata = metadata;
        runtime_session_->enqueue_command(command);
        runtime_session_->drain_pending_commands(1);

        const OrderResult order_snapshot = runtime_session_->snapshot_order(order_id);
        if (!runtime_session_->is_running() && should_queue_order_until_runtime_ready(order_snapshot)) {
            last_error_.clear();
            return build_pending_confirmation_order_id(order_id);
        }

        if (QString::fromStdString(order_snapshot.status).trimmed().compare(QStringLiteral("REJECTED"), Qt::CaseInsensitive) == 0) {
            last_error_ = order_snapshot.message.empty()
                ? std::string("Broker rejected order request")
                : order_snapshot.message;
            return "";
        }

        if (event_bus_) {
            engine::EventFormat event = engine::EventFormat::create_from_strings(
                engine::EventTypes::TRADING_ORDER_UPDATED,
                "JUJIN_API",
                0);
            event.set("account_id", config_.account_id);
            const std::string strategy_id = display_strategy_id_from_config(config_);
            if (!strategy_id.empty()) {
                event.set("strategy_id", strategy_id);
                event.metadata["strategy_id"] = strategy_id;
            }
            const std::string business_strategy_id = business_strategy_id_from_config(config_);
            if (!business_strategy_id.empty()) {
                event.set("business_strategy_id", business_strategy_id);
                event.metadata["business_strategy_id"] = business_strategy_id;
            }
            const std::string runtime_strategy_id = runtime_strategy_id_from_config(config_);
            if (!runtime_strategy_id.empty()) {
                event.set("runtime_strategy_id", runtime_strategy_id);
                event.metadata["runtime_strategy_id"] = runtime_strategy_id;
            }
            event.set("order_id", order_id);
            event.set("client_order_id", order_id);
            event.set("symbol", symbol);
            event.set("side", side == OrderSide::BUY ? "BUY" : "SELL");
            event.set("order_type", type == OrderType::MARKET ? "MARKET" : "LIMIT");
            event.set("price", price);
            event.set("quantity", static_cast<int64_t>(quantity));
            event.set("filled_quantity", order_snapshot.filled_quantity);
            event.set("filled_notional", order_snapshot.filled_notional);
            event.set("avg_price", order_snapshot.avg_price);
            event.set("status", order_snapshot.status.empty() ? "SUBMITTED" : order_snapshot.status);
            event.set("message", order_snapshot.message.empty() ? "Order submitted to broker runtime" : order_snapshot.message);
            event.set("created_at", order_snapshot.submit_time.empty() ? QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toStdString() : order_snapshot.submit_time);
            event.set("updated_at", order_snapshot.update_time.empty() ? QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toStdString() : order_snapshot.update_time);
            event.metadata["order_id"] = order_id;
            event.metadata["client_order_id"] = order_id;
            event.metadata["symbol"] = symbol;
            event.metadata["side"] = side == OrderSide::BUY ? "BUY" : "SELL";
            event.metadata["status"] = order_snapshot.status.empty() ? "SUBMITTED" : order_snapshot.status;
            event.metadata["status_origin"] = "runtime";
            event.metadata["event_contract"] = "canonical";
            for (const auto& [key, value] : metadata) {
                event.metadata[key] = value;
            }
            event_bus_->publish(event, static_cast<int>(engine::EventPriority::HIGH));
        }

        last_error_.clear();
        return order_id;
    }

    bool cancel_order(const std::string& order_id)
    {
        QMutexLocker locker(&mutex_);

        if (!connected_) {
            last_error_ = "未连接到券商";
            return false;
        }

        if (order_id.empty()) {
            last_error_ = "订单ID不能为空";
            return false;
        }

        if (!ensure_runtime_session_locked()) {
            last_error_ = "交易运行时会话不可用";
            return false;
        }

        TradingCommand command;
        command.type = TradingCommandType::CancelOrder;
        command.account_id = config_.account_id;
        command.strategy_id = runtime_strategy_id_from_config(config_);
        command.order_id = order_id;
        runtime_session_->enqueue_command(command);
        runtime_session_->drain_pending_commands(1);

        if (event_bus_) {
            engine::EventFormat event = engine::EventFormat::create_from_strings(
                engine::EventTypes::TRADING_ORDER_CANCEL_REQUEST,
                "JUJIN_API",
                0);
            event.set("account_id", config_.account_id);
            const std::string strategy_id = display_strategy_id_from_config(config_);
            if (!strategy_id.empty()) {
                event.set("strategy_id", strategy_id);
                event.metadata["strategy_id"] = strategy_id;
            }
            const std::string business_strategy_id = business_strategy_id_from_config(config_);
            if (!business_strategy_id.empty()) {
                event.set("business_strategy_id", business_strategy_id);
                event.metadata["business_strategy_id"] = business_strategy_id;
            }
            const std::string runtime_strategy_id = runtime_strategy_id_from_config(config_);
            if (!runtime_strategy_id.empty()) {
                event.set("runtime_strategy_id", runtime_strategy_id);
                event.metadata["runtime_strategy_id"] = runtime_strategy_id;
            }
            event.set("order_id", order_id);
            event.set("client_order_id", order_id);
            event.set("status", "PENDING_CANCEL");
            event.metadata["client_order_id"] = order_id;
            event.metadata["status_origin"] = "runtime";
            event.metadata["event_contract"] = "canonical";
            event_bus_->publish(event, static_cast<int>(engine::EventPriority::HIGH));
        }

        last_error_.clear();
        return true;
    }


    bool subscribe_market_data(const std::vector<std::string>& symbols,
                               MarketDataType type,
                               const std::map<std::string, std::string>& options)
    {
        Q_UNUSED(options);

        QMutexLocker locker(&mutex_);

        if (!connected_) {
            last_error_ = "未连接到券商";
            return false;
        }

        if (!ensure_runtime_session_locked()) {
            last_error_ = "交易运行时会话不可用";
            return false;
        }

        const std::string frequency = type == MarketDataType::BAR_1M ? "bar1m" : "tick";
        bool submitted = false;
        for (const std::string& symbol : symbols) {
            if (symbol.empty()) {
                continue;
            }

            TradingCommand command;
            command.type = TradingCommandType::Subscribe;
            command.account_id = config_.account_id;
            command.strategy_id = runtime_strategy_id_from_config(config_);
            command.symbol = symbol;
            command.frequency = frequency;
            runtime_session_->enqueue_command(command);
            submitted = true;
        }

        if (!submitted) {
            last_error_ = "订阅标的不能为空";
            return false;
        }

        if (runtime_session_->is_running()) {
            runtime_session_->drain_pending_commands(symbols.size());
        }
        last_error_.clear();
        return true;
    }
    std::vector<Position> query_positions()
    {
        QMutexLocker locker(&mutex_);
        std::vector<Position> positions;

        if (!connected_) {
            last_error_ = "未连接到券商";
            return positions;
        }

        if (ensure_runtime_session_locked()) {
            TradingCommand command;
            command.type = TradingCommandType::QueryPositions;
            command.account_id = config_.account_id;
            command.strategy_id = runtime_strategy_id_from_config(config_);
            runtime_session_->enqueue_command(command);
            runtime_session_->drain_pending_commands(1);
            positions = runtime_session_->snapshot_positions();
        }

        last_error_.clear();
        return positions;
    }

    AccountInfo query_account()
    {
        QMutexLocker locker(&mutex_);
        AccountInfo info;

        if (!connected_) {
            last_error_ = "未连接到券商";
            return info;
        }

        if (ensure_runtime_session_locked()) {
            TradingCommand command;
            command.type = TradingCommandType::QueryAccount;
            command.account_id = config_.account_id;
            command.strategy_id = runtime_strategy_id_from_config(config_);
            runtime_session_->enqueue_command(command);
            runtime_session_->drain_pending_commands(1);
            info = runtime_session_->snapshot_account();
        }

        if (info.update_time.empty()) {
            info.update_time = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toStdString();
        }
        last_error_.clear();
        return info;
    }

    OrderResult query_order(const std::string& order_id)
    {
        QMutexLocker locker(&mutex_);
        OrderResult result;

        if (!connected_) {
            last_error_ = "未连接到券商";
            return result;
        }

        if (order_id.empty()) {
            last_error_ = "订单ID不能为空";
            return result;
        }

        if (ensure_runtime_session_locked()) {
            TradingCommand command;
            command.type = TradingCommandType::QueryOrders;
            command.account_id = config_.account_id;
            command.strategy_id = runtime_strategy_id_from_config(config_);
            command.order_id = order_id;
            runtime_session_->enqueue_command(command);
            runtime_session_->drain_pending_commands(1);
            result = runtime_session_->snapshot_order(order_id);
        }

        if (result.order_id.empty()) {
            result.order_id = order_id;
            result.status = "PENDING";
            result.message = "Pending runtime snapshot";
            result.update_time = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toStdString();
        }
        last_error_.clear();
        return result;
    }

    std::vector<OrderResult> query_orders(const std::string& symbol,
                                          const std::string& status,
                                          int limit)
    {
        Q_UNUSED(limit);

        QMutexLocker locker(&mutex_);
        std::vector<OrderResult> orders;

        if (!connected_) {
            last_error_ = "未连接到券商";
            return orders;
        }

        if (ensure_runtime_session_locked()) {
            TradingCommand command;
            command.type = TradingCommandType::QueryOrders;
            command.account_id = config_.account_id;
            command.strategy_id = runtime_strategy_id_from_config(config_);
            command.symbol = symbol;
            command.metadata["status"] = status;
            runtime_session_->enqueue_command(command);
            runtime_session_->drain_pending_commands(1);
            orders = runtime_session_->snapshot_orders();
            if (!symbol.empty()) {
                orders.erase(
                    std::remove_if(
                        orders.begin(),
                        orders.end(),
                        [&symbol](const OrderResult& order) {
                            return order.symbol != symbol;
                        }),
                    orders.end());
            }
            if (!status.empty()) {
                orders.erase(
                    std::remove_if(
                        orders.begin(),
                        orders.end(),
                        [&status](const OrderResult& order) {
                            return order.status != status;
                        }),
                    orders.end());
            }
        }

        last_error_.clear();
        return orders;
    }

    void set_event_bus(std::shared_ptr<engine::EventBus> bus)
    {
        QMutexLocker locker(&mutex_);
        event_bus_ = bus;
        TradingRuntimeManager::instance().set_event_bus(bus);
        if (runtime_session_) {
            runtime_session_->set_event_bus(bus);
        }
    }

    std::string last_error_message() const
    {
        QMutexLocker locker(&mutex_);
        return last_error_;
    }

    void clear_error()
    {
        QMutexLocker locker(&mutex_);
        last_error_.clear();
    }

    ConfigParams get_config() const
    {
        QMutexLocker locker(&mutex_);
        return config_;
    }

    bool check_connection() const
    {
        QMutexLocker locker(&mutex_);
        return connected_ && runtime_session_;
    }

    std::string get_connection_status() const
    {
        QMutexLocker locker(&mutex_);
        if (!initialized_) {
            return "未初始化";
        }
        if (!runtime_session_) {
            return "无运行时会话";
        }
        if (!connected_) {
            return "未连接";
        }
        if (!runtime_session_->is_running()) {
            return "运行时未启动";
        }
        return "已连接";
    }

private:
    std::shared_ptr<GmStrategySession> ensure_runtime_session_locked()
    {
        if (config_.account_id.empty()) {
            return nullptr;
        }

        if (runtime_session_) {
            return runtime_session_;
        }

        TradingRuntimeManager& manager = TradingRuntimeManager::instance();
        manager.set_event_bus(event_bus_);

        runtime_session_ = manager.get_session(config_.account_id);
        if (!runtime_session_) {
            runtime_session_ = manager.create_session(config_);
        }
        return runtime_session_;
    }

    void publish_connection_event_locked(const std::string& event_type)
    {
        if (!event_bus_) {
            return;
        }

        engine::EventFormat event = engine::EventFormat::create_from_strings(event_type, "JUJIN_API", 0);
        event.set("platform", config_.platform == PlatformType::JUJIN ? "juejin" : "simulation");
        event.set("account_id", config_.account_id);
        const std::string strategy_id = display_strategy_id_from_config(config_);
        if (!strategy_id.empty()) {
            event.set("strategy_id", strategy_id);
            event.metadata["strategy_id"] = strategy_id;
        }
        const std::string business_strategy_id = business_strategy_id_from_config(config_);
        if (!business_strategy_id.empty()) {
            event.set("business_strategy_id", business_strategy_id);
            event.metadata["business_strategy_id"] = business_strategy_id;
        }
        const std::string runtime_strategy_id = runtime_strategy_id_from_config(config_);
        if (!runtime_strategy_id.empty()) {
            event.set("runtime_strategy_id", runtime_strategy_id);
            event.metadata["runtime_strategy_id"] = runtime_strategy_id;
        }
        event_bus_->publish(event, static_cast<int>(engine::EventPriority::HIGH));
    }

    std::string generate_order_id()
    {
        static int counter = 0;
        std::stringstream ss;
        ss << "JUEJIN_ORDER_" << QDateTime::currentDateTime().toString("yyyyMMddHHmmss").toStdString()
           << "_" << ++counter;
        return ss.str();
    }

    mutable QMutex mutex_;
    bool initialized_;
    bool connected_;
    ConfigParams config_;
    std::shared_ptr<engine::EventBus> event_bus_;
    std::shared_ptr<GmStrategySession> runtime_session_;
    std::string last_error_;
};

JujinApi::JujinApi()
    : impl_(std::make_unique<Impl>())
{
}

JujinApi::~JujinApi() = default;

bool JujinApi::initialize(const ConfigParams& config)
{
    return impl_->initialize(config);
}

bool JujinApi::connect()
{
    return impl_->connect();
}

bool JujinApi::disconnect()
{
    return impl_->disconnect();
}

bool JujinApi::is_connected() const
{
    return impl_->is_connected();
}

bool JujinApi::is_initialized() const
{
    return impl_->is_initialized();
}

std::string JujinApi::place_order(const std::string& symbol,
                                  OrderSide side,
                                  OrderType type,
                                  double price,
                                  double quantity,
                                  const std::string& client_order_id,
                                  const std::map<std::string, std::string>& metadata)
{
    return impl_->place_order(symbol, side, type, price, quantity, client_order_id, metadata);
}

bool JujinApi::cancel_order(const std::string& order_id)
{
    return impl_->cancel_order(order_id);
}

bool JujinApi::subscribe_market_data(const std::vector<std::string>& symbols,
                                     MarketDataType type,
                                     const std::map<std::string, std::string>& options)
{
    return impl_->subscribe_market_data(symbols, type, options);
}

std::vector<Position> JujinApi::query_positions()
{
    return impl_->query_positions();
}

AccountInfo JujinApi::query_account()
{
    return impl_->query_account();
}

OrderResult JujinApi::query_order(const std::string& order_id)
{
    return impl_->query_order(order_id);
}

std::vector<OrderResult> JujinApi::query_orders(const std::string& symbol,
                                                const std::string& status,
                                                int limit)
{
    return impl_->query_orders(symbol, status, limit);
}

void JujinApi::set_event_bus(std::shared_ptr<engine::EventBus> bus)
{
    impl_->set_event_bus(bus);
}

std::string JujinApi::last_error_message() const
{
    return impl_->last_error_message();
}

void JujinApi::clear_error()
{
    impl_->clear_error();
}

ConfigParams JujinApi::get_config() const
{
    return impl_->get_config();
}

bool JujinApi::check_connection() const
{
    return impl_->check_connection();
}

std::string JujinApi::get_connection_status() const
{
    return impl_->get_connection_status();
}

} // namespace thirdparty





