#pragma once
#include "IExecutor.h"
namespace foundation {
namespace thread {
class InlineExecutor : public IExecutor {
public:
    void post(std::function<void()> task) override {
        task();
    }

    bool isInExecutorThread() const override {
        return true;
    }
};
}
}