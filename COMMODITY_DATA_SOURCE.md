# 商品涨价推动行情数据来源说明

## 数据来源架构

### 1. **期货数据获取** (FuturesDataProvider)

已实现的 `FuturesDataProvider` 通过 **akshare** 获取期货市场数据:

```python
# 主要API
ak.futures_zh_spot()           # 期货实时行情
ak.futures_zh_daily_sina()     # 期货日线数据
ak.futures_main_sina()         # 主力合约数据
```

**支持的商品品种**:
- 黑色系: RB(螺纹钢), I(铁矿石), J(焦炭), HC(热轧卷板)
- 有色金属: CU(铜), AL(铝), ZN(锌), NI(镍)
- 能源化工: SC(原油), FU(燃料油), TA(PTA), EG(乙二醇), PP(聚丙烯)
- 农产品: M(豆粕), C(玉米), A(豆一)
- 煤炭: ZC(动力煤)

### 2. **商品产业链分析** (CommodityChainAnalyzer)

新创建的 `commodity_chain.py` 提供完整的产业链映射:

```python
INDUSTRY_CHAIN = {
    'steel_chain': {        # 钢铁产业链
        'upstream': {...},   # 上游原材料 (铁矿石、焦炭)
        'midstream': {...},  # 中游生产 (螺纹钢)
        'downstream': {...}  # 下游应用 (建筑、机械)
    },
    # ... 6大产业链
}
```

### 3. **数据流程**

```
akshare API → FuturesDataProvider → CommodityChainAnalyzer → 受益股票
     ↓                ↓                      ↓                    ↓
  期货价格      实时/历史数据        产业链传导分析         选股信号
```

## 实际应用示例

### 示例1: 螺纹钢涨价 → 钢铁股受益

```python
# 1. 获取期货价格
futures_provider = FuturesDataProvider()
realtime_data = futures_provider.get_realtime_data(['RB'])  # 螺纹钢

# 2. 分析产业链影响
analyzer = CommodityChainAnalyzer()
beneficiaries = analyzer.find_beneficiary_stocks('RB', 'up')

# 结果: ['600019.SH', '000709.SZ', '000708.SZ', ...]
# 宝钢股份、河钢股份、中信特钢等
```

### 示例2: 铁矿石涨价 → 钢铁股承压

```python
# 铁矿石是钢铁的原材料，涨价是负面影响
beneficiaries = analyzer.find_beneficiary_stocks('I', 'down')
# 铁矿石下跌时钢铁股受益
```

### 示例3: 原油涨价 → 石化股受益

```python
beneficiaries = analyzer.find_beneficiary_stocks('SC', 'up')
# 结果: ['600028.SH', '601857.SH', '600688.SH']
# 中国石化、中国石油、上海石化
```

## 主要功能

### 1. 产业链影响分析
```python
impacts = analyzer.analyze_commodity_impact(commodity_prices)
# 返回:
# - upstream_benefits: 上游原材料受益
# - midstream_benefits: 中游生产受益
# - downstream_pressure: 下游应用承压
```

### 2. 价格传导路径
```python
paths = analyzer.get_price_transmission_path('RB')
# 显示完整的价格传导链条
```

### 3. 成本压力指数
```python
pressure = analyzer.calculate_cost_pressure_index(commodity_prices, '钢铁产业链')
# 返回 0-1 的压力指数
```

### 4. 自动生成分析报告
```python
report = analyzer.generate_commodity_report(commodity_prices)
# 生成完整的商品价格传导分析报告
```

## 6大产业链覆盖

1. **钢铁产业链**: 铁矿石 → 螺纹钢 → 建筑/机械
2. **石油化工链**: 原油 → PTA/乙二醇 → 纺织/塑料
3. **有色金属链**: 铜/铝 → 电子/家电/新能源
4. **农产品链**: 豆粕/玉米 → 饲料/养殖
5. **煤电链**: 动力煤 → 电力
6. **新能源链**: 锂/钴/镍 → 电池材料 → 新能源车

## 数据更新频率

- **实时行情**: `get_realtime_data()` - 分钟级
- **日线数据**: `get_data(DataType.FUTURES_DAILY)` - 日级
- **主力合约**: 自动识别成交量最大的合约

## 使用方式

### 快速开始
```python
from astock_engine.data.providers import FuturesDataProvider
from astock_engine.data.commodity_chain import CommodityChainAnalyzer

# 初始化
futures_provider = FuturesDataProvider()
futures_provider.initialize()
analyzer = CommodityChainAnalyzer()

# 获取实时价格
realtime = futures_provider.get_realtime_data(['RB', 'CU', 'SC'])

# 分析影响
impacts = analyzer.analyze_commodity_impact(realtime.data)

# 找受益股
stocks = analyzer.find_beneficiary_stocks('RB', 'up')
```

### 完整示例
查看 `astock_engine/examples/commodity_chain_demo.py` 了解6个详细用例

## 技术特点

✅ **完整产业链覆盖**: 6大产业链，50+商品品种  
✅ **正负相关处理**: 区分正相关(产品)和负相关(原材料)  
✅ **实时数据支持**: 接入akshare实时期货行情  
✅ **智能阈值判断**: 5%/10%/20%三级变化等级  
✅ **双向传导分析**: 上游→中游→下游完整链条  
✅ **自动报告生成**: 一键生成分析报告

## 数据源说明

**akshare** 是专业的中国金融数据接口:
- 覆盖股票、期货、期权、基金等
- 数据来自东方财富、新浪财经等权威源
- 免费开源，无需API密钥
- 实时更新，延迟小于1分钟

**安装**:
```bash
pip install akshare>=1.8.0
```

## 注意事项

1. **交易时间**: 期货交易时间为工作日 9:00-15:00, 21:00-23:00
2. **节假日**: 数据在节假日不更新
3. **主力合约**: 自动识别并使用成交量最大的主力合约
4. **滞后性**: 商品价格变化传导到股票有1-3天滞后
5. **复杂性**: 实际影响还需考虑供需、政策等多因素

## 后续扩展方向

- [ ] 加入库存数据 (上海期货交易所库存)
- [ ] 加入基差数据 (期货-现货价差)
- [ ] 加入持仓数据 (多空持仓变化)
- [ ] 加入外盘联动 (LME金属、NYMEX原油)
- [ ] 加入季节性分析 (农产品季节规律)
- [ ] 加入机器学习预测 (价格趋势预测)
