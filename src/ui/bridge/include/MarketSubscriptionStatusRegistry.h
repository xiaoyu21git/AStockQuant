#pragma once

#include <mutex>

class MarketSubscriptionStatusRegistry {
public:
    static void update(int subscriptionCount, int subscriptionLimit, bool active)
    {
        std::lock_guard<std::mutex> lock(mutexRef());
        subscriptionCountRef() = subscriptionCount;
        subscriptionLimitRef() = subscriptionLimit;
        activeRef() = active;
    }

    static int subscriptionCount()
    {
        std::lock_guard<std::mutex> lock(mutexRef());
        return subscriptionCountRef();
    }

    static int subscriptionLimit()
    {
        std::lock_guard<std::mutex> lock(mutexRef());
        return subscriptionLimitRef();
    }

    static bool active()
    {
        std::lock_guard<std::mutex> lock(mutexRef());
        return activeRef();
    }

private:
    static std::mutex& mutexRef()
    {
        static std::mutex mutex;
        return mutex;
    }

    static int& subscriptionCountRef()
    {
        static int value = 0;
        return value;
    }

    static int& subscriptionLimitRef()
    {
        static int value = 0;
        return value;
    }

    static bool& activeRef()
    {
        static bool value = false;
        return value;
    }
};