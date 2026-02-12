// demo_gm_strategy.cpp
// 掘金 C++ SDK 功能测试 Demo

#include "../gmsdk/strategy.h"
#include <iostream>

using namespace std;

class DemoStrategy : public Strategy {
public:
    DemoStrategy(const char* token, const char* strategy_id, int mode)
        : Strategy(token, strategy_id, mode) {}

    void on_init() override {
        cout << "[on_init] 初始化，订阅行情..." << endl;
        subscribe("SHSE.600000", "tick");
        subscribe("SHSE.600000", "bar1m");
        // 查询账户、持仓、资金
        auto accs = get_accounts();
        if (accs) {
            for (int i = 0; i < accs->size(); ++i) {
                cout << "账户: " << accs->at(i).account_id << endl;
            }
        }
        auto pos = get_position();
        if (pos) {
            for (int i = 0; i < pos->size(); ++i) {
                cout << "持仓: " << pos->at(i).symbol << " 数量: " << pos->at(i).volume << endl;
            }
        }
        auto cash = get_cash();
        if (cash) {
            for (int i = 0; i < cash->size(); ++i) {
                cout << "资金: " << cash->at(i).available << endl;
            }
        }
    }

    void on_tick(Tick* tick) override {
        cout << "[on_tick] " << tick->symbol << " 最新价: " << tick->last_price << endl;
        if (!has_ordered) {
            // 下单测试
            Order order = order_volume(tick->symbol, 100, 1, 1, 1, tick->last_price); // 买入开仓限价单
            cout << "下单结果: " << order.cl_ord_id << " 状态: " << order.status << endl;
            has_ordered = true;
        }
    }

    void on_bar(Bar* bar) override {
        cout << "[on_bar] " << bar->symbol << " 收盘价: " << bar->close << endl;
    }

    void on_order_status(Order* order) override {
        cout << "[on_order_status] 订单: " << order->cl_ord_id << " 状态: " << order->status << endl;
        // 撤单测试
        if (order->status == 0) { // 假设0为未成交
            int ret = order_cancel(order->cl_ord_id);
            cout << "撤单结果: " << ret << endl;
        }
    }

    void on_position(Position* position) override {
        cout << "[on_position] " << position->symbol << " 持仓: " << position->volume << endl;
    }

    void on_cash(Cash* cash) override {
        cout << "[on_cash] 资金: " << cash->available << endl;
    }

    void on_error(int error_code, const char* error_msg) override {
        cerr << "[on_error] 错误码: " << error_code << " 信息: " << error_msg << endl;
    }

private:
    bool has_ordered = false;
};

int main(int argc, char* argv[]) {
    // token、策略ID、mode需根据实际环境填写
    const char* token = "your_token";
    const char* strategy_id = "demo_strategy";
    int mode = 2; // 2为回测，1为实盘

    DemoStrategy strategy(token, strategy_id, mode);
    strategy.run();
    return 0;
}
