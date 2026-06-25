// Redis缓存管理器实现 - hiredis同步客户端

#ifdef _WIN32
#include <winsock2.h>
#endif

#include "redis_cache_manager.h"
#include "foundation/log/logging.hpp"

#include <hiredis.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>

namespace AStockQuantEngine {
namespace Cache {

namespace {

static struct timeval toTimeval(std::chrono::milliseconds duration)
{
    struct timeval tv{};
    const auto milliseconds = duration.count();
    tv.tv_sec = static_cast<long>(milliseconds / 1000);
    tv.tv_usec = static_cast<long>((milliseconds % 1000) * 1000);
    return tv;
}

static bool replyIsOk(redisReply* reply)
{
    return reply != nullptr && reply->type == REDIS_REPLY_STATUS && reply->str != nullptr && std::strcmp(reply->str, "OK") == 0;
}

} // namespace

class RedisCacheManager::Impl {
public:
    explicit Impl(const RedisCacheConfig& config) : config_(config) {}

    ~Impl() {
        shutdown();
    }

    bool initialize() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!config_.enabled) {
            return true;
        }

        if (config_.enableCluster || config_.enableTls) {
            INTERNAL_ERROR_STREAM << "RedisCacheManager: cluster/TLS mode is not supported by the vendored hiredis build";
            return false;
        }

        if (!connectLocked()) {
            return false;
        }

        initialized_ = true;
        INTERNAL_INFO_STREAM << "RedisCacheManager: initialized";
        return true;
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        disconnectLocked();
        initialized_ = false;
    }

    bool get(const std::string& key, std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return false;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "GET %b", key.data(), key.size()));
        if (reply == nullptr) {
            disconnectLocked();
            return false;
        }

        const bool hit = reply->type == REDIS_REPLY_STRING && reply->str != nullptr;
        if (hit) {
            value.assign(reply->str, reply->len);
            ++stats_.hits;
        } else {
            ++stats_.misses;
        }

        freeReplyObject(reply);
        return hit;
    }

    bool set(const std::string& key, const std::string& value, std::chrono::seconds ttl) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return false;
        }

        redisReply* reply = nullptr;
        if (ttl.count() > 0) {
            reply = static_cast<redisReply*>(redisCommand(
                context_,
                "SET %b %b EX %lld",
                key.data(), key.size(),
                value.data(), value.size(),
                static_cast<long long>(ttl.count())));
        } else {
            reply = static_cast<redisReply*>(redisCommand(
                context_,
                "SET %b %b",
                key.data(), key.size(),
                value.data(), value.size()));
        }

        if (reply == nullptr) {
            disconnectLocked();
            return false;
        }

        const bool success = replyIsOk(reply);
        if (success) {
            ++stats_.puts;
        }
        freeReplyObject(reply);
        return success;
    }

    bool remove(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return false;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "DEL %b", key.data(), key.size()));
        if (reply == nullptr) {
            disconnectLocked();
            return false;
        }

        const bool success = reply->type == REDIS_REPLY_INTEGER;
        if (success) {
            ++stats_.removes;
        }
        freeReplyObject(reply);
        return success;
    }

    bool exists(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return false;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "EXISTS %b", key.data(), key.size()));
        if (reply == nullptr) {
            disconnectLocked();
            return false;
        }

        const bool exists = reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
        freeReplyObject(reply);
        return exists;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "FLUSHDB"));
        if (reply == nullptr) {
            disconnectLocked();
            return;
        }

        freeReplyObject(reply);
    }

    std::map<std::string, std::string> getBulk(const std::vector<std::string>& keys) {
        std::map<std::string, std::string> result;
        for (const auto& key : keys) {
            std::string value;
            if (get(key, value)) {
                result.emplace(key, std::move(value));
            }
        }
        return result;
    }

    void setBulk(const std::map<std::string, std::string>& keyValues, std::chrono::seconds ttl) {
        for (const auto& [key, value] : keyValues) {
            set(key, value, ttl);
        }
    }

    CacheStats getStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    void resetStats() {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_ = CacheStats{};
    }

    void setPolicy(const std::string& keyPrefix, CachePolicy policy) {
        std::lock_guard<std::mutex> lock(mutex_);
        policies_[keyPrefix] = policy;
    }

    CachePolicy getPolicy(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);

        for (const auto& [prefix, policy] : policies_) {
            if (key.rfind(prefix, 0) == 0) {
                return policy;
            }
        }

        CachePolicy policy;
        policy.ttl = std::chrono::seconds(300);
        policy.useLocalCache = false;
        policy.useRedisCache = true;
        return policy;
    }

    bool isEnabled() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && config_.enabled;
    }

    bool expire(const std::string& key, std::chrono::seconds ttl) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return false;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "EXPIRE %b %lld", key.data(), key.size(), static_cast<long long>(ttl.count())));
        if (reply == nullptr) {
            disconnectLocked();
            return false;
        }

        const bool success = reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
        freeReplyObject(reply);
        return success;
    }

    long long increment(const std::string& key, long long delta) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return -1;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "INCRBY %b %lld", key.data(), key.size(), delta));
        if (reply == nullptr) {
            disconnectLocked();
            return -1;
        }

        long long result = -1;
        if (reply->type == REDIS_REPLY_INTEGER) {
            result = reply->integer;
        }
        freeReplyObject(reply);
        return result;
    }

    long long decrement(const std::string& key, long long delta) {
        return increment(key, -delta);
    }

    bool hset(const std::string& key, const std::string& field, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return false;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(
            context_, "HSET %b %b %b",
            key.data(), key.size(),
            field.data(), field.size(),
            value.data(), value.size()));
        if (reply == nullptr) {
            disconnectLocked();
            return false;
        }

        const bool success = reply->type == REDIS_REPLY_INTEGER;
        freeReplyObject(reply);
        return success;
    }

    bool hget(const std::string& key, const std::string& field, std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return false;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(
            context_, "HGET %b %b",
            key.data(), key.size(),
            field.data(), field.size()));
        if (reply == nullptr) {
            disconnectLocked();
            return false;
        }

        bool success = false;
        if (reply->type == REDIS_REPLY_STRING && reply->str != nullptr) {
            value.assign(reply->str, reply->len);
            success = true;
        }
        freeReplyObject(reply);
        return success;
    }

    bool hdel(const std::string& key, const std::string& field) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return false;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "HDEL %b %b", key.data(), key.size(), field.data(), field.size()));
        if (reply == nullptr) {
            disconnectLocked();
            return false;
        }

        const bool success = reply->type == REDIS_REPLY_INTEGER;
        freeReplyObject(reply);
        return success;
    }

    std::map<std::string, std::string> hgetall(const std::string& key) {
        std::map<std::string, std::string> result;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return result;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "HGETALL %b", key.data(), key.size()));
        if (reply == nullptr) {
            disconnectLocked();
            return result;
        }

        if (reply->type == REDIS_REPLY_ARRAY) {
            for (size_t index = 0; index + 1 < reply->elements; index += 2) {
                redisReply* field = reply->element[index];
                redisReply* valueReply = reply->element[index + 1];
                if (field != nullptr && valueReply != nullptr && field->str != nullptr && valueReply->str != nullptr) {
                    result.emplace(std::string(field->str, field->len), std::string(valueReply->str, valueReply->len));
                }
            }
        }

        freeReplyObject(reply);
        return result;
    }

    bool sadd(const std::string& key, const std::string& member) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return false;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "SADD %b %b", key.data(), key.size(), member.data(), member.size()));
        if (reply == nullptr) {
            disconnectLocked();
            return false;
        }

        const bool success = reply->type == REDIS_REPLY_INTEGER;
        freeReplyObject(reply);
        return success;
    }

    bool srem(const std::string& key, const std::string& member) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return false;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "SREM %b %b", key.data(), key.size(), member.data(), member.size()));
        if (reply == nullptr) {
            disconnectLocked();
            return false;
        }

        const bool success = reply->type == REDIS_REPLY_INTEGER;
        freeReplyObject(reply);
        return success;
    }

    std::vector<std::string> smembers(const std::string& key) {
        std::vector<std::string> result;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return result;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "SMEMBERS %b", key.data(), key.size()));
        if (reply == nullptr) {
            disconnectLocked();
            return result;
        }

        if (reply->type == REDIS_REPLY_ARRAY) {
            result.reserve(reply->elements);
            for (size_t index = 0; index < reply->elements; ++index) {
                redisReply* member = reply->element[index];
                if (member != nullptr && member->str != nullptr) {
                    result.emplace_back(member->str, member->len);
                }
            }
        }

        freeReplyObject(reply);
        return result;
    }

    bool lpush(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return false;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "LPUSH %b %b", key.data(), key.size(), value.data(), value.size()));
        if (reply == nullptr) {
            disconnectLocked();
            return false;
        }

        const bool success = reply->type == REDIS_REPLY_INTEGER;
        freeReplyObject(reply);
        return success;
    }

    bool rpush(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return false;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "RPUSH %b %b", key.data(), key.size(), value.data(), value.size()));
        if (reply == nullptr) {
            disconnectLocked();
            return false;
        }

        const bool success = reply->type == REDIS_REPLY_INTEGER;
        freeReplyObject(reply);
        return success;
    }

    bool lpop(const std::string& key, std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return false;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "LPOP %b", key.data(), key.size()));
        if (reply == nullptr) {
            disconnectLocked();
            return false;
        }

        bool success = false;
        if (reply->type == REDIS_REPLY_STRING && reply->str != nullptr) {
            value.assign(reply->str, reply->len);
            success = true;
        }
        freeReplyObject(reply);
        return success;
    }

    bool rpop(const std::string& key, std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return false;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "RPOP %b", key.data(), key.size()));
        if (reply == nullptr) {
            disconnectLocked();
            return false;
        }

        bool success = false;
        if (reply->type == REDIS_REPLY_STRING && reply->str != nullptr) {
            value.assign(reply->str, reply->len);
            success = true;
        }
        freeReplyObject(reply);
        return success;
    }

    bool publish(const std::string& channel, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureConnectedLocked()) {
            return false;
        }

        redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "PUBLISH %b %b", channel.data(), channel.size(), message.data(), message.size()));
        if (reply == nullptr) {
            disconnectLocked();
            return false;
        }

        const bool success = reply->type == REDIS_REPLY_INTEGER;
        freeReplyObject(reply);
        return success;
    }

    bool isConnected() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && context_ != nullptr && context_->err == 0;
    }

    const RedisCacheConfig& getConfig() const {
        return config_;
    }

private:
    bool ensureConnectedLocked() {
        if (!initialized_) {
            return false;
        }

        if (context_ != nullptr && context_->err == 0) {
            return true;
        }

        return connectLocked();
    }

    bool connectLocked() {
        disconnectLocked();

        struct timeval connectTimeout = toTimeval(config_.connectTimeout);
        context_ = redisConnectWithTimeout(config_.host.c_str(), config_.port, connectTimeout);
        if (context_ == nullptr) {
            INTERNAL_ERROR_STREAM << "RedisCacheManager: failed to allocate Redis context";
            return false;
        }

        if (context_->err != 0) {
            INTERNAL_ERROR_STREAM << "RedisCacheManager: connect failed: " << context_->errstr;
            disconnectLocked();
            return false;
        }

        if (config_.operationTimeout.count() > 0) {
            struct timeval commandTimeout = toTimeval(config_.operationTimeout);
            if (redisSetTimeout(context_, commandTimeout) != REDIS_OK) {
                INTERNAL_ERROR_STREAM << "RedisCacheManager: failed to set command timeout";
                disconnectLocked();
                return false;
            }
        }

        if (!config_.password.empty()) {
            redisReply* authReply = static_cast<redisReply*>(redisCommand(context_, "AUTH %s", config_.password.c_str()));
            if (!replyIsOk(authReply)) {
                INTERNAL_ERROR_STREAM << "RedisCacheManager: AUTH failed";
                if (authReply != nullptr) {
                    freeReplyObject(authReply);
                }
                disconnectLocked();
                return false;
            }
            freeReplyObject(authReply);
        }

        if (config_.database != 0) {
            redisReply* selectReply = static_cast<redisReply*>(redisCommand(context_, "SELECT %d", config_.database));
            if (!replyIsOk(selectReply)) {
                INTERNAL_ERROR_STREAM << "RedisCacheManager: SELECT failed";
                if (selectReply != nullptr) {
                    freeReplyObject(selectReply);
                }
                disconnectLocked();
                return false;
            }
            freeReplyObject(selectReply);
        }

        return true;
    }

    void disconnectLocked() {
        if (context_ != nullptr) {
            redisFree(context_);
            context_ = nullptr;
        }
    }

private:
    RedisCacheConfig config_;
    mutable std::mutex mutex_;
    redisContext* context_{nullptr};
    bool initialized_{false};
    CacheStats stats_;
    std::map<std::string, CachePolicy> policies_;
};

RedisCacheManager::RedisCacheManager(const RedisCacheConfig& config)
    : pImpl(std::make_unique<Impl>(config)) {}

RedisCacheManager::~RedisCacheManager() = default;

bool RedisCacheManager::initialize() {
    return pImpl->initialize();
}

void RedisCacheManager::shutdown() {
    pImpl->shutdown();
}

bool RedisCacheManager::get(const std::string& key, std::string& value) {
    return pImpl->get(key, value);
}

bool RedisCacheManager::set(const std::string& key, const std::string& value,
                            std::chrono::seconds ttl) {
    return pImpl->set(key, value, ttl);
}

bool RedisCacheManager::remove(const std::string& key) {
    return pImpl->remove(key);
}

bool RedisCacheManager::exists(const std::string& key) {
    return pImpl->exists(key);
}

void RedisCacheManager::clear() {
    pImpl->clear();
}

std::map<std::string, std::string> RedisCacheManager::getBulk(const std::vector<std::string>& keys) {
    return pImpl->getBulk(keys);
}

void RedisCacheManager::setBulk(const std::map<std::string, std::string>& keyValues,
                                std::chrono::seconds ttl) {
    pImpl->setBulk(keyValues, ttl);
}

CacheStats RedisCacheManager::getStats() const {
    return pImpl->getStats();
}

void RedisCacheManager::resetStats() {
    pImpl->resetStats();
}

void RedisCacheManager::setPolicy(const std::string& keyPrefix, CachePolicy policy) {
    pImpl->setPolicy(keyPrefix, policy);
}

CachePolicy RedisCacheManager::getPolicy(const std::string& key) const {
    return pImpl->getPolicy(key);
}

bool RedisCacheManager::isEnabled() const {
    return pImpl->isEnabled();
}

bool RedisCacheManager::expire(const std::string& key, std::chrono::seconds ttl) {
    return pImpl->expire(key, ttl);
}

long long RedisCacheManager::increment(const std::string& key, long long delta) {
    return pImpl->increment(key, delta);
}

long long RedisCacheManager::decrement(const std::string& key, long long delta) {
    return pImpl->decrement(key, delta);
}

bool RedisCacheManager::hset(const std::string& key, const std::string& field, const std::string& value) {
    return pImpl->hset(key, field, value);
}

bool RedisCacheManager::hget(const std::string& key, const std::string& field, std::string& value) {
    return pImpl->hget(key, field, value);
}

bool RedisCacheManager::hdel(const std::string& key, const std::string& field) {
    return pImpl->hdel(key, field);
}

std::map<std::string, std::string> RedisCacheManager::hgetall(const std::string& key) {
    return pImpl->hgetall(key);
}

bool RedisCacheManager::sadd(const std::string& key, const std::string& member) {
    return pImpl->sadd(key, member);
}

bool RedisCacheManager::srem(const std::string& key, const std::string& member) {
    return pImpl->srem(key, member);
}

std::vector<std::string> RedisCacheManager::smembers(const std::string& key) {
    return pImpl->smembers(key);
}

bool RedisCacheManager::lpush(const std::string& key, const std::string& value) {
    return pImpl->lpush(key, value);
}

bool RedisCacheManager::rpush(const std::string& key, const std::string& value) {
    return pImpl->rpush(key, value);
}

bool RedisCacheManager::lpop(const std::string& key, std::string& value) {
    return pImpl->lpop(key, value);
}

bool RedisCacheManager::rpop(const std::string& key, std::string& value) {
    return pImpl->rpop(key, value);
}

bool RedisCacheManager::publish(const std::string& channel, const std::string& message) {
    return pImpl->publish(channel, message);
}

bool RedisCacheManager::isConnected() const {
    return pImpl->isConnected();
}

const RedisCacheConfig& RedisCacheManager::getConfig() const {
    return pImpl->getConfig();
}

} // namespace Cache
} // namespace AStockQuantEngine
