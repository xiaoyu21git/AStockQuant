// GmSessionEngine.cpp — 唯一 gmsdk 入口实现
#include "GmSessionEngine.h"
#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"
#include "foundation/config/ConfigManager.hpp"
#include "../../../thirdparty/gmsdk/strategy.h"

#include <cstdio>
#include <ctime>

namespace engine {

// ═══════════════════════════════════════════════════════════════════
// GmQuote
// ═══════════════════════════════════════════════════════════════════

double GmQuote::limitPct() const {
    if (symbol.size() < 3) return 10.0;
    std::string code = symbol.substr(0, symbol.size() - 3);
    if (code.size() == 6 && (code[0] == '3' || (code[0] == '6' && code[1] == '8'))) return 20.0;
    if (symbol[symbol.size() - 2] == 'B') return 30.0;
    return 10.0;
}
double GmQuote::changePct() const { return preClose > 0 ? (price - preClose) / preClose * 100.0 : 0; }
bool GmQuote::isLimitUp()   const { return preClose > 0 && changePct() >= limitPct() - 0.05; }
bool GmQuote::isLimitDown() const { return preClose > 0 && changePct() <= -limitPct() + 0.05; }

// ═══════════════════════════════════════════════════════════════════
// 内部 Strategy 子类 — 统一处理所有 gmsdk 回调
// ═══════════════════════════════════════════════════════════════════

namespace {

class SessionStrategy : public ::Strategy {
public:
    SessionStrategy(const std::string& token, const std::string& strategyId,
                    GmSessionEngine::Impl* impl)
        : m_token(token), m_strategyId(strategyId), m_impl(impl) {
        set_token(token.c_str());
        if (!strategyId.empty()) set_strategy_id(strategyId.c_str());
        auto cfg = foundation::config::ConfigManager::instance()
            .loadConfigFile(foundation::config::ConfigFile::TradingConnection);
        int mode = 1;
        std::string accountId;
        if (cfg && !cfg->isNull()) {
            if (cfg->has("mode")) { auto m = cfg->get("mode"); mode = m.isNumber() ? m.asInt() : 1; }
            bool useSimAccount = (mode == 2
                              || (cfg->has("simtradeOnly") && cfg->get("simtradeOnly").asBool())
                              || (cfg->has("accountProfile") && cfg->get("accountProfile").asString() == "simulation"));
            accountId = useSimAccount
                ? (cfg->has("simAccountId") ? cfg->get("simAccountId").asString() : "")
                : (cfg->has("liveAccountId") ? cfg->get("liveAccountId").asString() : "");
            if (accountId.empty())
                accountId = cfg->has("accountId") ? cfg->get("accountId").asString() : "";
        }
        if (!accountId.empty()) set_account_id(accountId.c_str());
        set_mode(mode);
    }


    void on_init() override {
        try {
        auto* bus = get_engine_event_bus();
        if (!bus || !bus->is_running()) return;
        auto* arr = get_cash(nullptr);
        if (arr && arr->status() == 0 && arr->count() > 0) {
            auto& cash = arr->at(0);
            EventFormat evt("trading.account.updated", Event_Core::EventSource::MARKET_DATA);
            evt.set("account_id", cash.account_id);
            evt.set("available", cash.available);
            evt.set("total_asset", cash.nav);
            evt.set("market_value", cash.market_value);
            evt.set("frozen", cash.frozen);
            bus->publish(evt, static_cast<int>(EventPriority::HIGH));
        }
        if (arr) arr->release();
        auto* posArr = get_position(nullptr);
        if (posArr && posArr->status() == 0 && posArr->count() > 0) {
            for (size_t i = 0; i < posArr->count(); ++i) {
                auto& p = posArr->at(i);
                EventFormat evt("trading.position.updated", Event_Core::EventSource::MARKET_DATA);
                evt.set("symbol", GmSessionEngine::fromGmSymbol(p.symbol));
                evt.set("quantity", static_cast<int64_t>((p.side == 2) ? -p.volume : p.volume));
                evt.set("available_qty", static_cast<int64_t>(p.available));
                evt.set("cost_price", p.vwap);
                evt.set("last_price", p.price);
                evt.set("market_value", p.market_value);
                evt.set("unrealized_pnl", p.fpnl);
                bus->publish(evt, static_cast<int>(EventPriority::HIGH));
            }
        }
        if (posArr) posArr->release();
        } catch (const std::exception& e) {
            INTERNAL_ERROR_STREAM << "[GmSdk] on_init exception: " << e.what();
        } catch (...) {
            INTERNAL_ERROR_STREAM << "[GmSdk] on_init unknown exception";
        }
    }

    void on_tick(Tick* tick) override {
        if (!tick) return;
        try {
        auto& e = GmSessionEngine::instance();
        GmTickData td;
        td.symbol     = e.fromGmSymbol(tick->symbol);
        td.price      = static_cast<double>(tick->price);
        td.open       = static_cast<double>(tick->open);
        td.high       = static_cast<double>(tick->high);
        td.low        = static_cast<double>(tick->low);
        td.cumVolume  = tick->cum_volume;
        td.cumAmount  = tick->cum_amount;
        td.lastVolume = static_cast<double>(tick->last_volume);
        auto tt = static_cast<time_t>(tick->created_at);
        struct tm local; localtime_s(&local, &tt);
        td.tradingDay = (local.tm_year + 1900) * 10000LL + (local.tm_mon + 1) * 100LL + local.tm_mday;
        for (int i = 0; i < 5; ++i) {
            auto& q = tick->quotes[i];
            if (q.bid_price > 0) { td.bidPrices.push_back(q.bid_price); td.bidVolumes.push_back(q.bid_volume); }
            if (q.ask_price > 0) { td.askPrices.push_back(q.ask_price); td.askVolumes.push_back(q.ask_volume); }
        }

        auto* bus = get_engine_event_bus();
        if (bus && bus->is_running()) {
            EventFormat evt("trading.market.tick", Event_Core::EventSource::MARKET_DATA);
            evt.set("symbol", td.symbol);
            evt.set("price", td.price);
            evt.set("open", td.open);
            evt.set("high", td.high);
            evt.set("low", td.low);
            evt.set("volume", td.lastVolume > 0 ? td.lastVolume : td.cumVolume);
            evt.set("tradingDay", td.tradingDay);
            bus->publish(evt, static_cast<int>(EventPriority::HIGH));
        }
        } catch (const std::exception& e) {
            INTERNAL_ERROR_STREAM << "[GmSdk] on_tick exception: " << e.what();
        } catch (...) {
            INTERNAL_ERROR_STREAM << "[GmSdk] on_tick unknown exception";
        }
    }

    void on_order_status(Order* o) override {
        if (!o) return;
        auto& e = GmSessionEngine::instance();
        OrderUpdate u;
        u.brokerOrderId = o->cl_ord_id;
        u.symbol        = e.fromGmSymbol(o->symbol);
        u.filledPrice   = static_cast<double>(o->filled_vwap);
        u.filledQuantity = static_cast<int64_t>(o->filled_volume);
        u.message       = o->ord_rej_reason_detail;
        switch (o->status) {
            case 2: u.status = OrderUpdate::PartialFilled; break;
            case 3: u.status = OrderUpdate::Filled;        break;
            case 5: u.status = OrderUpdate::Cancelled;     break;
            case 6: u.status = OrderUpdate::Rejected;      break;
            case 7: u.status = OrderUpdate::Expired;       break;
            case 8: u.status = OrderUpdate::Rejected;      break; // 废单/拒绝
            default: u.status = OrderUpdate::Submitted;    break;
        }

        INTERNAL_INFO_STREAM << "[GmSdk] on_order_status cl_ord_id=" << u.brokerOrderId
                             << " symbol=" << u.symbol
                             << " gmStatus=" << o->status
                             << " mappedStatus=" << static_cast<int>(u.status)
                             << " filledVol=" << u.filledQuantity
                             << " filledPrice=" << u.filledPrice
                             << " msg=" << u.message;

        auto* bus = get_engine_event_bus();
        if (bus && bus->is_running()) {
            EventFormat evt("trading.order.updated", Event_Core::EventSource::MARKET_DATA);
            evt.set("broker_order_id", u.brokerOrderId);
            evt.set("symbol", u.symbol);
            evt.set("filled_price", u.filledPrice);
            evt.set("filled_quantity", static_cast<int64_t>(u.filledQuantity));
            evt.set("status", static_cast<int64_t>(u.status));
            evt.set("message", u.message);
            bus->publish(evt, static_cast<int>(EventPriority::HIGH));
        } else {
            INTERNAL_ERROR_STREAM << "[GmSdk] on_order_status EventBus not running, order update dropped";
        }
    }

    void on_execution_report(ExecRpt* rpt) override {
        if (!rpt) return;
        auto& e = GmSessionEngine::instance();
        TradeFill f;
        f.fillId = rpt->exec_id; f.brokerOrderId = rpt->cl_ord_id;
        f.symbol = e.fromGmSymbol(rpt->symbol);
        f.price = static_cast<double>(rpt->price);
        f.quantity = static_cast<int64_t>(rpt->volume);
        f.commission = static_cast<double>(rpt->commission);

        auto* bus = get_engine_event_bus();
        if (bus && bus->is_running()) {
            EventFormat evt("trading.execution.report", Event_Core::EventSource::MARKET_DATA);
            evt.set("exec_id", f.fillId);
            evt.set("broker_order_id", f.brokerOrderId);
            evt.set("symbol", f.symbol);
            evt.set("price", f.price);
            evt.set("quantity", static_cast<int64_t>(f.quantity));
            evt.set("commission", f.commission);
            bus->publish(evt, static_cast<int>(EventPriority::HIGH));
        }
    }

    void on_cash(Cash* cash) override {
        if (!cash) return;
        auto& e = GmSessionEngine::instance();
        AccountInfo a;
        a.accountId = cash->account_id; a.totalAsset = cash->nav;
        a.availableCash = cash->available; a.marketValue = cash->market_value;
        a.frozenCash = cash->frozen;

        auto* bus = get_engine_event_bus();
        if (bus && bus->is_running()) {
            EventFormat evt("trading.account.updated", Event_Core::EventSource::MARKET_DATA);
            evt.set("account_id", a.accountId);
            evt.set("available", a.availableCash);
            evt.set("total_asset", a.totalAsset);
            evt.set("market_value", a.marketValue);
            evt.set("frozen", a.frozenCash);
            bus->publish(evt, static_cast<int>(EventPriority::HIGH));
        }
    }

    void on_position(::Position* pos) override {
        if (!pos) return;
        auto& e = GmSessionEngine::instance();
        Position p;
        p.symbol = e.fromGmSymbol(pos->symbol);
        p.quantity = (pos->side == 2) ? -pos->volume : pos->volume;
        p.availableQty = pos->available; p.costPrice = pos->vwap;
        p.lastPrice = pos->price; p.marketValue = pos->market_value;
        p.unrealizedPnl = pos->fpnl;

        auto* bus = get_engine_event_bus();
        if (bus && bus->is_running()) {
            EventFormat evt("trading.position.updated", Event_Core::EventSource::MARKET_DATA);
            evt.set("symbol", p.symbol);
            evt.set("quantity", p.quantity);
            evt.set("available_qty", p.availableQty);
            evt.set("cost_price", p.costPrice);
            evt.set("last_price", p.lastPrice);
            evt.set("market_value", p.marketValue);
            evt.set("unrealized_pnl", p.unrealizedPnl);
            bus->publish(evt, static_cast<int>(EventPriority::HIGH));
        }
    }

private:
    std::string m_token, m_strategyId;
    GmSessionEngine::Impl* m_impl = nullptr;
};

int toGmSide(OrderRequest::Side s) { return s == OrderRequest::Buy ? 1 : 2; }
int toGmType(OrderRequest::Type t) { return t == OrderRequest::Limit ? 1 : 2; }
std::string fromGm(const std::string& gm) {
    if (gm.compare(0, 5, "SHSE.") == 0) return gm.substr(5) + ".SH";
    if (gm.compare(0, 5, "SZSE.") == 0) return gm.substr(5) + ".SZ";
    if (gm.compare(0, 4, "BSE.")  == 0) return gm.substr(4) + ".BJ";
    return gm;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════
// StrategyDeleter
// ═══════════════════════════════════════════════════════════════════

void GmSessionEngine::StrategyDeleter::operator()(void* p) {
    delete static_cast<SessionStrategy*>(p);
}

// ═══════════════════════════════════════════════════════════════════
// 单例 + 生命周期
// ═══════════════════════════════════════════════════════════════════

GmSessionEngine& GmSessionEngine::instance() {
    static GmSessionEngine e; return e;
}
GmSessionEngine::~GmSessionEngine() { shutdown(); }

bool GmSessionEngine::initialize(const std::string& token, const std::string& accountId) {
    if (m_impl && m_impl->initialized.load()) return true;
    if (token.empty()) return false;
    m_impl = std::make_unique<Impl>();
    m_strategy.reset(new SessionStrategy(token, accountId, m_impl.get()));
    m_impl->strategyThread = std::thread([this]() {
        try {
            static_cast<SessionStrategy*>(m_strategy.get())->run();
        } catch (const std::exception& e) {
            INTERNAL_ERROR_STREAM << "[GmSession] run() exception: " << e.what();
        } catch (...) {
            INTERNAL_ERROR_STREAM << "[GmSession] run() unknown exception";
        }
    });
    m_impl->initialized.store(true);
    return true;
}

void GmSessionEngine::shutdown() {
    if (!m_impl || !m_impl->initialized.load()) return;
    m_impl->initialized.store(false);
    if (m_impl->strategyThread.joinable()) m_impl->strategyThread.detach();
    m_strategy.reset(); m_impl.reset();
}

bool GmSessionEngine::initialized() const { return m_impl && m_impl->initialized.load(); }

// ═══════════════════════════════════════════════════════════════════
// 行情订阅
// ═══════════════════════════════════════════════════════════════════

void GmSessionEngine::subscribeTick(const std::string& symbol) {
    if (symbol.empty()) return;
    std::lock_guard<std::mutex> lock(m_tickMutex);
    if (++m_tickRefCount[symbol] == 1) {
        auto* s = static_cast<SessionStrategy*>(m_strategy.get());
        if (s) s->subscribe(toGmSymbol(symbol).c_str(), "tick", false);
    }
}

void GmSessionEngine::unsubscribeTick(const std::string& symbol) {
    if (symbol.empty()) return;
    std::lock_guard<std::mutex> lock(m_tickMutex);
    auto it = m_tickRefCount.find(symbol);
    if (it != m_tickRefCount.end() && --it->second <= 0)
        m_tickRefCount.erase(it);
}

// ═══════════════════════════════════════════════════════════════════
// 回调设置
// ═══════════════════════════════════════════════════════════════════


// ═══════════════════════════════════════════════════════════════════
// 行情查询
// ═══════════════════════════════════════════════════════════════════

std::optional<GmQuote> GmSessionEngine::fetchQuote(const std::string& symbol) {
    std::string gm = toGmSymbol(symbol); if (gm.empty()) return std::nullopt;
    auto* arr = ::current(gm.c_str(), false);
    if (!arr || arr->status() || !arr->count()) {
        if (arr) arr->release();
        arr = ::last_tick(gm.c_str(), false);
    }
    if (!arr || arr->status() || !arr->count()) { if (arr) arr->release(); return std::nullopt; }
    auto& t = arr->at(0);
    GmQuote q; q.symbol = symbol; q.valid = true;
    q.price = t.price; q.open = t.open; q.high = t.high; q.low = t.low;
    q.preClose = fetchPreClose(symbol); q.volume = t.cum_volume;
    for (int i = 0; i < 5; ++i) {
        if (t.quotes[i].bid_price > 0) q.bids.push_back({static_cast<double>(t.quotes[i].bid_price), static_cast<double>(t.quotes[i].bid_volume)});
        if (t.quotes[i].ask_price > 0) q.asks.push_back({static_cast<double>(t.quotes[i].ask_price), static_cast<double>(t.quotes[i].ask_volume)});
    }
    arr->release(); return q;
}

double GmSessionEngine::fetchPreClose(const std::string& symbol) {
    time_t now = time(nullptr); char today[16]; strftime(today, sizeof(today), "%Y%m%d", localtime(&now));
    if (m_cacheDate != today) { m_preCloseCache.clear(); m_cacheDate = today; }
    auto it = m_preCloseCache.find(symbol); if (it != m_preCloseCache.end()) return it->second;
    std::string gm = toGmSymbol(symbol); if (gm.empty()) return 0;
    char s[32], e[32]; time_t y = now - 86400;
    strftime(s, sizeof(s), "%Y-%m-%d", localtime(&y)); strftime(e, sizeof(e), "%Y-%m-%d", localtime(&now));
    auto* bars = ::history_bars(gm.c_str(), "1d", s, e, 0, nullptr, true, nullptr);
    double pc = (bars && !bars->status() && bars->count()) ? bars->at(0).close : 0;
    if (bars) bars->release();
    return m_preCloseCache[symbol] = pc;
}

// ═══════════════════════════════════════════════════════════════════
// 下单
// ═══════════════════════════════════════════════════════════════════


// ═══════════════════════════════════════════════════════════════════

void* GmSessionEngine::strategy() const { return m_strategy.get(); }


// 符号转换
// ═══════════════════════════════════════════════════════════════════

std::string GmSessionEngine::toGmSymbol(const std::string& in) {
    auto d = in.find('.'); if (d == std::string::npos) return "";
    std::string c = in.substr(0, d), e = in.substr(d + 1);
    if (e == "SH") return "SHSE." + c; if (e == "SZ") return "SZSE." + c;
    if (e == "BJ") return "BSE." + c; return "";
}
std::string GmSessionEngine::fromGmSymbol(const std::string& gm) {
    if (gm.compare(0, 5, "SHSE.") == 0) return gm.substr(5) + ".SH";
    if (gm.compare(0, 5, "SZSE.") == 0) return gm.substr(5) + ".SZ";
    if (gm.compare(0, 4, "BSE.")  == 0) return gm.substr(4) + ".BJ";
    return gm;
}

} // namespace engine
