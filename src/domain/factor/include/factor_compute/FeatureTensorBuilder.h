#pragma once
// FeatureTensorBuilder — 从 HistoricalView 构建模型输入张量
// 推理时使用训练保存的 mean/std 做归一化，确保与训练一致

#include "domain/factor/include/HistoricalView.h"
#include "foundation/json/json_facade.h"

#include <string>
#include <vector>

namespace factor::compute {

struct FeatureTensor {
    std::vector<float> data;          // 展平的张量数据 [N*W*F] (与 ONNX 输入对齐)
    std::vector<int64_t> shape;       // {N, W, F}
    std::vector<std::string> symbols; // 标的顺序 (对应 N 维)
    int featureCount{0};
    int lookbackWindow{0};
    bool valid{false};
};

class FeatureTensorBuilder {
public:
    /// @param fields        输入字段列表 (如 {"close","volume","turnover_rate","market_cap"})
    /// @param lookbackWindow 回看窗口天数
    /// @param scalerPath    可选 scaler.json 路径。为空则使用在线 ZScore (仅测试用)
    explicit FeatureTensorBuilder(const std::vector<std::string>& fields,
                                   int lookbackWindow,
                                   const std::string& scalerPath = "");

    /// @brief 从 HistoricalView 构建张量
    FeatureTensor build(const factor::HistoricalView& view,
                        const std::vector<std::string>& symbols,
                        const std::string& anchorDate);

    /// @brief 已加载的字段名 (与训练时顺序一致)
    const std::vector<std::string>& fields() const { return m_fields; }
    int featureCount() const { return static_cast<int>(m_fields.size()); }

    /// @brief 训练导出的 scaler 是否成功加载（false=回退到在线 zscore，与训练不一致）
    bool hasScaler() const { return m_hasScaler; }

private:
    /// 使用 ZScore 标准化 (在线计算，无 scaler 时用)
    static std::vector<double> zscore(const std::vector<double>& series);

    /// 使用预训练 scaler 标准化 (生产路径)
    std::vector<double> scalerNormalize(const std::vector<double>& series, int featureIdx) const;

    /// 从原始特征计算派生特征 (与 train.py add_derived_features 严格一致)
    static std::vector<std::vector<double>> addDerivedFeatures(
        const std::vector<std::vector<double>>& rawSeries,  // F_raw × [W 天]
        const std::vector<std::string>& rawFields,
        int nTimeSteps);

    bool loadScaler(const std::string& path);

    std::vector<std::string> m_fields;
    int m_lookbackWindow;
    size_t m_rawFieldCount{0};           // 原始字段数, 0=无派生特征
    int m_marketFeatureCount{0};         // 市场特征数, 0=无市场特征
    std::vector<double> m_scalerMeans;   // 训练时的均值
    std::vector<double> m_scalerScales;  // 训练时的标准差
    std::vector<double> m_winsorLo;      // 特征1%分位数
    std::vector<double> m_winsorHi;      // 特征99%分位数
    bool m_hasScaler{false};             // 是否成功加载 scaler
};

} // namespace factor::compute