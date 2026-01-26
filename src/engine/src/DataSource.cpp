#include "IDataSource.h"
#include <algorithm>
#include "Event.h"

namespace engine {

// ==================== 修改点1: 修复基类构造函数 ====================
// 原代码: DataSource::DataSource(std::string name, std::string uri) { ... }
// 问题: 构造函数为空，没有初始化基类成员
DataSource::DataSource(std::string name, std::string uri)
    : name_(std::move(name))    // 新增: 正确初始化基类成员
    , uri_(std::move(uri)) {    // 新增: 正确初始化基类成员
    // 抽象类构造函数，具体实现在子类中
}

// DataSource的具体实现类
class DataSourceImpl : public DataSource {
public:
    // ==================== 修改点2: 修复构造函数初始化列表 ====================
    // 原代码: DataSourceImpl(std::string name, std::string uri)
    //         : DataSource(name, uri), name_(std::move(name)), uri_(std::move(uri)), ...
    // 问题: 重复初始化 name_ 和 uri_，传递给基类的参数错误
    DataSourceImpl(std::string name, std::string uri)
        : DataSource(std::move(name), std::move(uri))  // 修复: 使用 std::move 传递给基类
        , state_(DataListener::State::Disconnected)
        , poll_interval_(Duration(1'000'000'000)) // 默认1秒
        , should_stop_(false) {
        // 注意: 删除了 name_(std::move(name)) 和 uri_(std::move(uri))
    }
    
    ~DataSourceImpl() override {
        disconnect();
    }
    
    Error connect() override {
        if (state_ == DataListener::State::Connected) {
            return Error{Error::Code::CONNECTED, "Data source is already connected"};
        }
        
        auto old_state = state_;
        state_ = DataListener::State::Connecting;
        notify_state_changed(old_state, state_);
        
        // 这里实现具体的连接逻辑
        // 例如：连接数据库、WebSocket、文件等
        
        // ==================== 修改点3: 使用基类成员 ====================
        // 原代码: LOG_INFO("Connecting to data source: " + name_ + " at " + uri_);
        // 现在 name_ 和 uri_ 来自基类
        //LOG_INFO("Connecting to data source: " + name_ + " at " + uri_);
        
        // 连接成功
        old_state = state_;
        state_ = DataListener::State::Connected;
        notify_state_changed(old_state, state_);
        
        // 如果需要，启动轮询线程
        if (!uri_.empty() && uri_.find("file://") != 0) { // 不是文件数据源
            start_polling_thread();
        }
        
        return Error{Error::Code::OK, ""};
    }
    
    Error disconnect() override {
        if (state_ == DataListener::State::Disconnected) {
            return Error{Error::Code::DISCONNECTED, "Data source is already disconnected"};
        }
        
        auto old_state = state_;
        state_ = DataListener::State::Disconnected;
        notify_state_changed(old_state, state_);
        
        // 停止轮询线程
        stop_polling_thread();
        
        // ==================== 修改点4: 使用基类成员 ====================
        //LOG_INFO("Disconnected from data source: " + name_);
        
        return Error{Error::Code::OK, ""};
    }
    
    Error poll() override {
        if (state_ != DataListener::State::Connected) {
            return Error{Error::Code::DISCONNECTED, "Data source is not connected"};
        }
        
        // 这里实现具体的数据拉取逻辑
        // 例如：从API获取数据、读取文件等
        
        try {
            // ==================== 修改点5: 使用基类成员 ====================
           // LOG_DEBUG("Polling data from: " + name_);
            
            // 创建模拟事件
            auto event = Event::create(
                Event::Type::MarketData,
                foundation::timestamp_now(),
                {{"symbol", "AAPL"}, {"price", "150.25"}}
            );
            
            if (event) {
                notify_data_received(std::move(event));
            }
            
            return Error{Error::Code::OK, ""};
        } catch (const std::exception& e) {
            // ==================== 修改点6: 使用基类成员 ====================
            LOG_ERROR("Failed to poll data from " + name_ + ": " + e.what());
            
            // 更新状态为错误
            auto old_state = state_;
            state_ = DataListener::State::Error;
            notify_state_changed(old_state, state_);
            
            return Error{Error::Code::NOT_FOUND, "Poll failed: " + std::string(e.what())};
        }
    }
    
    // ==================== 修改点7: 实现基类纯虚函数 ====================
    std::string name() const override {
        return name_;  // 返回基类的 name_
    }
    
    std::string uri() const override {
        return uri_;  // 返回基类的 uri_
    }
    
    DataListener::State state() const override {
        return state_;
    }
    
    void register_listener(DataListener* listener) override {
        std::lock_guard<std::mutex> lock(listeners_mutex_);
        if (listener && std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end()) {
            listeners_.push_back(listener);
        }
    }
    
    void unregister_listener(DataListener* listener) override {
        std::lock_guard<std::mutex> lock(listeners_mutex_);
        auto it = std::find(listeners_.begin(), listeners_.end(), listener);
        if (it != listeners_.end()) {
            listeners_.erase(it);
        }
    }
    
    void set_poll_interval(Duration interval) override {
        std::lock_guard<std::mutex> lock(poll_mutex_);
        poll_interval_ = interval;
    }
    
private:
    // ==================== 修改点8: 删除重复的成员变量 ====================
    // 原代码有: std::string name_; std::string uri_;
    // 现在这些成员在基类中，子类不应该重复定义
    
    DataListener::State state_;
    Duration poll_interval_;
    
    std::vector<DataListener*> listeners_;
    mutable std::mutex listeners_mutex_;
    
    std::unique_ptr<std::thread> poll_thread_;
    bool should_stop_;
    mutable std::mutex poll_mutex_;
    
    void start_polling_thread() {
        std::lock_guard<std::mutex> lock(poll_mutex_);
        
        if (poll_thread_ && poll_thread_->joinable()) {
            return; // 线程已经在运行
        }
        
        should_stop_ = false;
        poll_thread_ = std::make_unique<std::thread>([this]() {
            polling_loop();
        });
    }
    
    void stop_polling_thread() {
        {
            std::lock_guard<std::mutex> lock(poll_mutex_);
            should_stop_ = true;
        }
        
        if (poll_thread_ && poll_thread_->joinable()) {
            poll_thread_->join();
            poll_thread_.reset();
        }
    }
    
    void polling_loop() {
        while (true) {
            {
                std::lock_guard<std::mutex> lock(poll_mutex_);
                if (should_stop_) {
                    break;
                }
            }
            
            // 执行轮询
            if (state_ == DataListener::State::Connected) {
                poll();
            }
            
            // 等待下一次轮询
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    void notify_data_received(std::unique_ptr<Event> event) {
        std::lock_guard<std::mutex> lock(listeners_mutex_);
        for (auto* listener : listeners_) {
            try {
                listener->on_data_received(std::unique_ptr<Event>(event->clone().release()));
            } catch (const std::exception& e) {
                LOG_ERROR("Data listener error: " + std::string(e.what()));
            }
        }
    }
    
    void notify_state_changed(DataListener::State old_state, DataListener::State new_state) {
        std::lock_guard<std::mutex> lock(listeners_mutex_);
        for (auto* listener : listeners_) {
            try {
                listener->on_state_changed(old_state, new_state);
            } catch (const std::exception& e) {
                std::cerr <<"Data listener error: " + std::string(e.what());
            }
        }
    }
};

// 工厂方法实现
std::unique_ptr<DataSource> DataSource::create(const std::string& name, const std::string& uri) {
    return std::make_unique<DataSourceImpl>(name, uri);
}

} // namespace engine