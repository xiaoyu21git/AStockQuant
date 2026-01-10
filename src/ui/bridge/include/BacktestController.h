// BacktestController.h
#pragma once
#include <QObject>

class BacktestController : public QObject {
    Q_OBJECT
public:
    explicit BacktestController(QObject* parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void run();

signals:
    // ... 如果有信号
    
public slots:
    // ... 如果有槽
};
