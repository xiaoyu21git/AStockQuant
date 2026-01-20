// market/utils/ObjectPool.h
#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include <cstddef>

namespace astock {
namespace utils {

// 线程安全对象池
template<typename T, size_t ChunkSize = 1024>
class ObjectPool {
public:
    ObjectPool() = default;
    ~ObjectPool() {
        clear();
    }
    
    // 获取对象（构造或从池中取）
    template<typename... Args>
    std::shared_ptr<T> acquire(Args&&... args) {
        std::unique_lock lock(mutex_);
        
        if (!free_list_.empty()) {
            auto ptr = free_list_.back();
            free_list_.pop_back();
            lock.unlock();
            
            // 使用placement new在已有内存上构造
            new(ptr.get()) T(std::forward<Args>(args)...);
            return std::shared_ptr<T>(
                ptr.get(),
                [this](T* obj) { release(obj); });
        }
        
        // 池为空，分配新内存
        auto chunk = std::make_unique<Chunk>();
        auto ptr = &chunk->objects[chunk->next_index++];
        
        // 构造对象
        new(ptr) T(std::forward<Args>(args)...);
        
        // 保存chunk所有权
        chunks_.push_back(std::move(chunk));
        
        return std::shared_ptr<T>(
            ptr,
            [this](T* obj) { release(obj); });
    }
    
    // 预留容量
    void reserve(size_t count) {
        std::unique_lock lock(mutex_);
        size_t needed = (count - total_capacity() + ChunkSize - 1) / ChunkSize;
        for (size_t i = 0; i < needed; ++i) {
            allocate_chunk();
        }
    }
    
    // 清空池
    void clear() {
        std::unique_lock lock(mutex_);
        chunks_.clear();
        free_list_.clear();
    }
    
    // 统计信息
    size_t total_capacity() const {
        std::unique_lock lock(mutex_);
        return chunks_.size() * ChunkSize;
    }
    
    size_t free_count() const {
        std::unique_lock lock(mutex_);
        return free_list_.size();
    }
    
    size_t in_use_count() const {
        return total_capacity() - free_count();
    }
    
private:
    struct Chunk {
        std::array<T, ChunkSize> objects;
        size_t next_index = 0;
    };
    
    void allocate_chunk() {
        auto chunk = std::make_unique<Chunk>();
        chunks_.push_back(std::move(chunk));
    }
    
    void release(T* obj) {
        // 调用析构函数
        obj->~T();
        
        std::unique_lock lock(mutex_);
        free_list_.push_back(std::unique_ptr<T[]>(obj));
    }
    
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<Chunk>> chunks_;
    std::vector<std::unique_ptr<T[]>> free_list_;
};

} // namespace utils
}