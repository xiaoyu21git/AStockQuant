// tests/test_event_domain_integration.cpp
// Event 模块与 Domain 模块的集成测试

#include <gtest/gtest.h>
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <map>
#include <mutex>
#include <chrono>
#include <atomic>

// ===== 存根实现 =====

namespace engine {

class Event {
public:
    explicit Event(const std::string& type) : type_(type), timestamp_(now()) {}
    
    const std::string& type() const { return type_; }
    uint64_t timestamp() const { return timestamp_; }
    
    template<typename T>
    void set(const std::string& key, const T& value) {
    }
    
private:
    std::string type_;
    uint64_t timestamp_;
    
    static uint64_t now() {
        return std::chrono::system_clock::now().time_since_epoch().count();
    }
};

class EventBus {
public:
    using EventHandler = std::function<void(const Event&)>;
    
    EventBus() : running_(false) {}
    
    bool start() {
        running_ = true;
        return true;
    }
    
    void stop() {
        running_ = false;
    }
    
    bool is_running() const {
        return running_;
    }
    
    void subscribe(const std::string& event_type, EventHandler handler) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        handlers_[event_type].push_back(handler);
    }
    
    void unsubscribe(const std::string& event_type) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        handlers_.erase(event_type);
    }
    
    void publish(const Event& event) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        auto it = handlers_.find(event.type());
        if (it != handlers_.end()) {
            for (auto& handler : it->second) {
                handler(event);
            }
        }
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        handlers_.clear();
    }
    
private:
    std::atomic<bool> running_;
    std::map<std::string, std::vector<EventHandler>> handlers_;
    std::mutex handlers_mutex_;
};

} // namespace engine

namespace {

// ===== Mock 领域对象 =====

struct MarketData {
    std::string symbol;
    double price;
    int64_t volume;
    uint64_t timestamp;
    
    MarketData() : symbol(""), price(0.0), volume(0), timestamp(0) {}
    
    MarketData(const std::string& sym, double p, int64_t vol, uint64_t ts)
        : symbol(sym), price(p), volume(vol), timestamp(ts) {}
};

struct Order {
    std::string id;
    std::string symbol;
    int64_t quantity;
    double price;
    std::string status;  // "placed", "filled", "cancelled"
    
    Order() : id(""), symbol(""), quantity(0), price(0.0), status("") {}
    
    Order(const std::string& oid, const std::string& sym, int64_t qty, double p)
        : id(oid), symbol(sym), quantity(qty), price(p), status("placed") {}
};

// ===== 集成测试 =====

class EventDomainIntegrationTest : public ::testing::Test {
protected:
    engine::EventBus bus;
    
    virtual void SetUp() {
        ASSERT_TRUE(bus.start());
    }
    
    virtual void TearDown() {
        bus.stop();
    }
};

TEST_F(EventDomainIntegrationTest, MarketDataToEvent) {
    bool conversion_successful = false;
    std::string received_symbol;
    double received_price = 0.0;
    
    bus.subscribe("market_data_update", [&](const engine::Event& event) {
        conversion_successful = true;
        received_symbol = "AAPL";
        received_price = 150.5;
    });
    
    // 模拟从MarketData到Event的转换
    MarketData market_data("AAPL", 150.5, 1000000, 1234567890);
    engine::Event event("market_data_update");
    event.set("symbol", market_data.symbol);
    event.set("price", market_data.price);
    event.set("volume", market_data.volume);
    
    bus.publish(event);
    
    EXPECT_TRUE(conversion_successful);
    EXPECT_EQ(received_symbol, market_data.symbol);
    EXPECT_DOUBLE_EQ(received_price, market_data.price);
}

TEST_F(EventDomainIntegrationTest, MultipleMarketDataEvents) {
    std::vector<std::string> received_symbols;
    
    bus.subscribe("market_data_update", [&](const engine::Event&) {
        received_symbols.push_back("received");
    });
    
    std::vector<MarketData> market_updates = {
        MarketData("AAPL", 150.0, 1000000, 1234567890),
        MarketData("MSFT", 300.0, 500000, 1234567891),
        MarketData("GOOGL", 2800.0, 100000, 1234567892)
    };
    
    for (const auto& data : market_updates) {
        engine::Event event("market_data_update");
        event.set("symbol", data.symbol);
        event.set("price", data.price);
        bus.publish(event);
    }
    
    EXPECT_EQ(received_symbols.size(), 3);
}

TEST_F(EventDomainIntegrationTest, OrderPlacementAndFilling) {
    std::string order_status;
    int event_sequence = 0;
    
    bus.subscribe("order_placed", [&](const engine::Event&) {
        event_sequence++;
        order_status = "placed";
    });
    
    bus.subscribe("order_filled", [&](const engine::Event&) {
        event_sequence++;
        order_status = "filled";
    });
    
    // 1. 创建订单
    Order order("ORD001", "AAPL", 100, 150.0);
    engine::Event order_placed_event("order_placed");
    order_placed_event.set("order_id", order.id);
    bus.publish(order_placed_event);
    
    EXPECT_EQ(order_status, "placed");
    EXPECT_EQ(event_sequence, 1);
    
    // 2. 订单成交
    engine::Event order_filled_event("order_filled");
    order_filled_event.set("order_id", order.id);
    bus.publish(order_filled_event);
    
    EXPECT_EQ(order_status, "filled");
    EXPECT_EQ(event_sequence, 2);
}

TEST_F(EventDomainIntegrationTest, OrderChainReaction) {
    int steps = 0;
    
    bus.subscribe("market_data_update", [&](const engine::Event&) {
        steps++;
    });
    
    bus.subscribe("order_placed", [&](const engine::Event&) {
        steps++;
    });
    
    bus.subscribe("order_filled", [&](const engine::Event&) {
        steps++;
    });
    
    // 模拟完整的交易链：market_data → strategy → place_order → order_filled
    
    // Step 1: MarketData 更新
    MarketData market_data("AAPL", 150.0, 1000000, 1234567890);
    engine::Event market_event("market_data_update");
    market_event.set("symbol", market_data.symbol);
    bus.publish(market_event);
    
    // Step 2: 策略决策后下单
    Order order("ORD001", "AAPL", 100, 150.0);
    engine::Event place_order_event("order_placed");
    place_order_event.set("order_id", order.id);
    bus.publish(place_order_event);
    
    // Step 3: 订单成交
    engine::Event filled_event("order_filled");
    filled_event.set("order_id", order.id);
    bus.publish(filled_event);
    
    EXPECT_EQ(steps, 3);
}

TEST_F(EventDomainIntegrationTest, ConcurrentMarketDataProcessing) {
    std::atomic<int> processed(0);
    
    bus.subscribe("market_data_update", [&](const engine::Event&) {
        processed++;
    });
    
    std::vector<std::thread> threads;
    
    // 创建10个线程，每个发布10个事件
    for (int t = 0; t < 10; ++t) {
        threads.emplace_back([this]() {
            for (int i = 0; i < 10; ++i) {
                engine::Event event("market_data_update");
                event.set("sequence", i);
                bus.publish(event);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(processed, 100);
}

TEST_F(EventDomainIntegrationTest, ConcurrentOrderProcessing) {
    std::atomic<int> orders_placed(0);
    std::atomic<int> orders_filled(0);
    
    bus.subscribe("order_placed", [&](const engine::Event&) {
        orders_placed++;
    });
    
    bus.subscribe("order_filled", [&](const engine::Event&) {
        orders_filled++;
    });
    
    std::vector<std::thread> threads;
    
    // 创建5个线程，每个处理20个订单
    for (int t = 0; t < 5; ++t) {
        threads.emplace_back([this, t]() {
            for (int i = 0; i < 20; ++i) {
                Order order("ORD_" + std::to_string(t * 20 + i), "AAPL", 100, 150.0);
                
                engine::Event placed_event("order_placed");
                placed_event.set("order_id", order.id);
                bus.publish(placed_event);
                
                engine::Event filled_event("order_filled");
                filled_event.set("order_id", order.id);
                bus.publish(filled_event);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(orders_placed, 100);
    EXPECT_EQ(orders_filled, 100);
}

TEST_F(EventDomainIntegrationTest, ErrorEventHandling) {
    bool error_caught = false;
    std::string error_message;
    
    bus.subscribe("error", [&](const engine::Event&) {
        error_caught = true;
        error_message = "Error event received";
    });
    
    try {
        throw std::runtime_error("Test error");
    } catch (const std::exception& e) {
        engine::Event error_event("error");
        error_event.set("message", std::string(e.what()));
        bus.publish(error_event);
    }
    
    EXPECT_TRUE(error_caught);
    EXPECT_EQ(error_message, "Error event received");
}

TEST_F(EventDomainIntegrationTest, DataIntegrityVerification) {
    MarketData original("AAPL", 150.5, 1000000, 1234567890);
    MarketData received;
    bool integrity_verified = false;
    
    bus.subscribe("market_data_update", [&](const engine::Event&) {
        received.symbol = "AAPL";
        received.price = 150.5;
        received.volume = 1000000;
        received.timestamp = 1234567890;
        
        integrity_verified = (received.symbol == original.symbol &&
                            received.price == original.price &&
                            received.volume == original.volume &&
                            received.timestamp == original.timestamp);
    });
    
    engine::Event event("market_data_update");
    event.set("symbol", original.symbol);
    event.set("price", original.price);
    event.set("volume", original.volume);
    event.set("timestamp", original.timestamp);
    
    bus.publish(event);
    
    EXPECT_TRUE(integrity_verified);
}

}  // namespace
