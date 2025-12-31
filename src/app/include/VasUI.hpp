#include <QQmlApplicationEngine>
namespace vasui{
class VasUI{
    public:
        VasUI(); 
    private:
        std::unique_ptr<QQmlApplicationEngine> engineM;       
};
}