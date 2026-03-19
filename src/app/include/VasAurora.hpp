
#pragma once
#include <QQmlApplicationEngine>

namespace wang {

class VasAurora {
public:
    explicit VasAurora(QQmlApplicationEngine* engine);

private:
    QQmlApplicationEngine* engineM; // 不再拥有，只使用
};

}