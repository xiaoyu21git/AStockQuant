#pragma once
#include "IExecutor.h"

class InlineExecutor : public IExecutor {
public:
    void post(std::function<void()> task) override {
        task();
    }

    bool isInExecutorThread() const override {
        return true;
    }
};
