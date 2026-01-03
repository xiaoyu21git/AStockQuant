// #pragma once
// #include <QQmlApplicationEngine>
// //#include <QQuickWidget>
// namespace wang{
// class VasAurora{
//     public:
//         VasAurora(); 
//     private:
//         std::unique_ptr<QQmlApplicationEngine> engineM;   
         
// };
// }


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