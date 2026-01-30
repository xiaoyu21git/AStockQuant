# 🚀 AStockQuantEngine 快速开始

## 📦 已完成核心模块

### ✅ 基础设施层 (100%)
- **EventBus系统**: C++高性能事件总线 (769K evt/s)
- **Engine框架**: 引擎核心架构
- **测试系统**: 30个测试用例全部通过

### ✅ 数据层 (100%)
- **StockDataProvider**: A股行情数据 (日线/分钟线/实时)
- **FuturesDataProvider**: 期货数据 (股指/商品) + 基差分析
- **NewsDataProvider**: 新闻舆情 + 情感分析

### ✅ 业务模块 (100%)
- **AnomalyDetector**: 异动监控 (涨停/跌停/放量/大单)
- **RiskManager**: 风险管理 (VaR/回撤/止损/仓位控制)

---

## 🎯 快速安装

### 1. 安装依赖

```bash
# 激活虚拟环境
.\.venv\Scripts\activate

# 安装Python依赖
pip install -r astock_engine/requirements.txt
```

### 2. 验证安装

```bash
python -c "import akshare as ak; print('AKShare版本:', ak.__version__)"
```

---

## 🏃 运行示例

### 完整演示示例

```bash
# 运行完整示例（包含所有功能演示）
python astock_engine/examples/complete_demo.py
```

这个示例展示了：
1. ✅ 获取股票历史数据
2. ✅ 实时行情监控与异动检测
3. ✅ 股指期货基差分析
4. ✅ 新闻舆情分析
5. ✅ 风险管理与监控
6. ✅ 完整工作流程

---

## 📖 核心功能使用

### 1. 获取股票数据

```python
from astock_engine.data.providers import StockDataProvider, DataQuery, DataType
from datetime import datetime

# 初始化
provider = StockDataProvider()
provider.initialize()

# 查询日线数据
query = DataQuery(
    symbols=['000001.SZ', '600000.SH'],
    start_date=datetime(2024, 1, 1).date(),
    end_date=datetime(2024, 12, 31).date(),
    adjust='qfq'
)

response = provider.get_data(query, DataType.STOCK_DAILY)
print(response.data.head())

# 获取实时行情
realtime = provider.get_realtime_data(['000001.SZ'])
print(realtime.data)
```

### 2. 异动监控

```python
from astock_engine.anomaly import AnomalyDetector

# 初始化检测器
detector = AnomalyDetector()

# 订阅异动事件
def on_anomaly(event):
    print(f"异动: {event.symbol} - {event.description}")

detector.subscribe(on_anomaly)

# 检测异动
anomalies = detector.detect(current_data, historical_data)
for anomaly in anomalies:
    print(f"{anomaly.anomaly_type.value}: {anomaly.description}")
```

### 3. 风险管理

```python
from astock_engine.risk import RiskManager

# 初始化风险管理器
risk_mgr = RiskManager()

# 计算风险指标
metrics = risk_mgr.calculate_metrics(
    portfolio_value=1000000,
    positions=positions_dict,
    equity_curve=equity_series
)

print(f"最大回撤: {metrics.max_drawdown:.2%}")
print(f"夏普比率: {metrics.sharpe_ratio:.2f}")
print(f"风险等级: {metrics.risk_level.value}")

# 检查风险限制
alerts = risk_mgr.check_limits(metrics, positions_dict)
for alert in alerts:
    print(f"风险预警: {alert.message}")
```

### 4. 期货基差分析

```python
from astock_engine.data.providers import FuturesDataProvider

# 初始化
futures_provider = FuturesDataProvider()
futures_provider.initialize()

# 获取主力合约
main_contracts = futures_provider.get_main_contracts(['IF', 'IH', 'IC'])

# 计算基差
basis_df = futures_provider.get_basis_data(
    index_symbol='000300.SH',  # 沪深300
    futures_symbol=main_contracts['IF']
)

print(f"平均基差: {basis_df['basis'].mean():.2f}点")
print(f"基差率: {basis_df['basis_rate'].mean():.2%}")
```

### 5. 新闻舆情分析

```python
from astock_engine.data.providers import NewsDataProvider, DataType

# 初始化
news_provider = NewsDataProvider()
news_provider.initialize()

# 获取新闻
query = DataQuery(symbols=['000001.SZ'])
response = news_provider.get_data(query, DataType.NEWS)

# 情感分析
for _, news in response.data.iterrows():
    sentiment = news_provider.analyze_sentiment(news['title'])
    print(f"{news['title']}: {sentiment['sentiment']} ({sentiment['score']:.2f})")

# 获取热门股票
hot_stocks = news_provider.get_hot_stocks(limit=10)
print(hot_stocks)
```

---

## 🔧 配置说明

### 异动检测阈值配置

```python
detector = AnomalyDetector(config={
    'thresholds': {
        'limit_up_rate': 9.9,          # 涨停阈值
        'rapid_rise_rate': 3.0,        # 急涨阈值(%)
        'volume_surge_ratio': 2.0,     # 放量倍数
        'big_order_amount': 1000000,   # 大单金额(元)
        'turnover_high': 20.0,         # 高换手率(%)
    }
})
```

### 风险管理限制配置

```python
risk_mgr = RiskManager(config={
    'limits': {
        'max_position_ratio': 0.30,    # 单股持仓上限30%
        'max_drawdown': 0.15,          # 最大回撤限制15%
        'stop_loss_ratio': -0.05,      # 止损线-5%
        'stop_profit_ratio': 0.20,     # 止盈线+20%
    }
})
```

---

## 📊 支持的数据类型

### 行情数据
- ✅ A股日线/分钟线/实时行情
- ✅ 股指期货 (IF/IH/IC/IM)
- ✅ 商品期货 (螺纹钢/铜/原油等)
- ✅ 指数数据 (上证/深证/创业板)

### 舆情数据
- ✅ 财经新闻 (东方财富/新浪)
- ✅ 公司公告
- ✅ 热门股票榜
- ✅ 股吧评论

### 异动类型
- ✅ 涨停/跌停
- ✅ 急涨/急跌
- ✅ 放量/缩量
- ✅ 大买单/大卖单
- ✅ 换手率异常
- ✅ 波动率突增

---

## 🎓 下一步开发

### Phase 2: 策略系统 (2周)
- [ ] 多因子选股策略
- [ ] 股期联动套利策略
- [ ] 商品期货驱动选股
- [ ] 舆情驱动事件策略

### Phase 3: 回测优化 (1周)
- [ ] 完善回测引擎
- [ ] 参数优化器
- [ ] 绩效分析报告
- [ ] 可视化Dashboard

---

## 📞 技术支持

遇到问题？查看：
- 完整示例: [complete_demo.py](astock_engine/examples/complete_demo.py)
- API文档: [base_provider.py](astock_engine/data/providers/base_provider.py)
- 测试代码: [astock_engine/tests/](astock_engine/tests/)

---

**开始你的量化之旅！** 🚀
