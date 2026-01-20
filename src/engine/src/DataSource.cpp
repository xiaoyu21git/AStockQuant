// engine/src/DataSource.cpp
#include "DataSource.h"
#include <algorithm>

namespace engine {

// 构造函数实现
DataSource::DataSource(std::string name, std::string uri) {
    // 抽象类构造函数，具体实现在子类中
}

// DataSource的具体实现类
class DataSourceImpl : public DataSource {
public:
    DataSourceImpl(std::string name, std::string uri)
        : name_(std::move(name))
        , uri_(std::move(uri))
        , state_(DataListener::State::Disconnected)
        , poll_interval_(Duration(1'000'000'000)) // 默认1秒
        , should_stop_(false) {
    }
    
    ~DataSourceImpl() override {
        disconnect();
    }
    
    Error connect() override {
        if (state_ == DataListener::State::Connected) {
            return Error{1, "Data source is already connected"};
        }
        
        auto old_state = state_;
        state_ = DataListener::State::Connecting;
        notify_state_changed(old_state, state_);
        
        // 这里实现具体的连接逻辑
        // 例如：连接数据库、WebSocket、文件等
        
        // 模拟连接过程
        LOG_INFO("Connecting to data source: " + name_ + " at " + uri_);
        
        // 连接成功
        old_state = state_;
        state_ = DataListener::State::Connected;
        notify_state_changed(old_state, state_);
        
        // 如果需要，启动轮询线程
        if (!uri_.empty() && uri_.find("file://") != 0) { // 不是文件数据源
            start_polling_thread();
        }
        
        return Error{0, ""};
    }
    
    Error disconnect() override {
        if (state_ == DataListener::State::Disconnected) {
            return Error{2, "Data source is already disconnected"};
        }
        
        auto old_state = state_;
        state_ = DataListener::State::Disconnected;
        notify_state_changed(old_state, state_);
        
        // 停止轮询线程
        stop_polling_thread();
        
        // 这里实现具体的断开连接逻辑
        LOG_INFO("Disconnected from data source: " + name_);
        
        return Error{0, ""};
    }
    
    Error poll() override {
        if (state_ != DataListener::State::Connected) {
            return Error{3, "Data source is not connected"};
        }
        
        // 这里实现具体的数据拉取逻辑
        // 例如：从API获取数据、读取文件等
        
        try {
            // 模拟数据获取
            LOG_DEBUG("Polling data from: " + name_);
            
            // 创建模拟事件
            auto event = std::make_unique<Event>(
                Event::Type::MarketData,
                std::chrono::system_clock::now(),
                name_
            );
            
            // 通知所有监听器
            notify_data_received(std::move(event));
            
            return Error{0, ""};
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to poll data from " + name_ + ": " + e.what());
            
            // 更新状态为错误
            auto old_state = state_;
            state_ = DataListener::State::Error;
            notify_state_changed(old_state, state_);
            
            return Error{4, "Poll failed: " + std::string(e.what())};
        }
    }
    
    std::string name() const override {
        return name_;
    }
    
    std::string uri() const override {
        return uri_;
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
            std::this_thread::sleep_for(std::chrono::duration_cast<std::chrono::milliseconds>(poll_interval_));
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
                LOG_ERROR("Data listener error: " + std::string(e.what()));
            }
        }
    }
    
    std::string name_;
    std::string uri_;
    DataListener::State state_;
    Duration poll_interval_;
    
    std::vector<DataListener*> listeners_;
    mutable std::mutex listeners_mutex_;
    
    std::unique_ptr<std::thread> poll_thread_;
    bool should_stop_;
    mutable std::mutex poll_mutex_;
};

// 工厂方法实现
std::unique_ptr<DataSource> DataSource::create(const std::string& name, const std::string& uri) {
    return std::make_unique<DataSourceImpl>(name, uri);
}

} // namespace engine