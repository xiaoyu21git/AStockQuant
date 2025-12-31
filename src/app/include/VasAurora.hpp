#pragma once
#include <QQmlApplicationEngine>
//#include <QQuickWidget>
namespace wang{
class VasAurora{
    public:
        VasAurora(); 
    private:
        std::unique_ptr<QQmlApplicationEngine> engineM;   
         
};
}