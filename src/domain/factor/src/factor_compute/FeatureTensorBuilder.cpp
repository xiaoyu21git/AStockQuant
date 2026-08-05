#include "factor_compute/FeatureTensorBuilder.h"

#include "foundation/log/logging.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numeric>
#include <sstream>
#include <unordered_map>

namespace {

// 与 train.py DERIVED_FEATURES 严格一致
const std::vector<std::string> kDerivedFields = {
    "ret_5d", "ret_20d", "vol_5d", "ma10_dev", "vol_ratio_5d", "turnover_chg",
};

// 与 train.py MARKET_FEATURES 严格一致 (需两轮扫描: 截面统计 → 拼特征)
const std::vector<std::string> kMarketFields = {
    "market_ret", "market_breadth", "market_volatility", "industry_rel_ret",
};

int fieldIndex(const std::vector<std::string>& fields, const std::string& name) {
    for (size_t i = 0; i < fields.size(); ++i)
        if (fields[i] == name) return static_cast<int>(i);
    return -1;
}

} // namespace

namespace factor::compute {

FeatureTensorBuilder::FeatureTensorBuilder(
    const std::vector<std::string>& fields, int lookbackWindow,
    const std::string& scalerPath)
    : m_fields(fields), m_lookbackWindow(lookbackWindow)
{
    if (!scalerPath.empty()) {
        std::filesystem::path modelDir = std::filesystem::path(scalerPath).parent_path();
        std::string scalerFile = (modelDir / "scaler.json").string();
        if (std::filesystem::exists(scalerFile)) {
            m_hasScaler = loadScaler(scalerFile);
            if (m_hasScaler) {
                const size_t nRaw = m_fields.size();
                const size_t nDerived = kDerivedFields.size();
                const size_t nMarket = kMarketFields.size();
                const size_t nScaler = m_scalerMeans.size();

                // 自动检测特征组合: raw, raw+derived, raw+market, raw+derived+market
                bool detected = false;
                // raw only
                if (nScaler == nRaw) {
                    detected = true;
                }
                // raw + derived
                if (nScaler == nRaw + nDerived) {
                    m_rawFieldCount = nRaw;
                    m_fields.insert(m_fields.end(), kDerivedFields.begin(), kDerivedFields.end());
                    detected = true;
                }
                // raw + market
                if (nScaler == nRaw + nMarket) {
                    m_marketFeatureCount = static_cast<int>(nMarket);
                    m_fields.insert(m_fields.end(), kMarketFields.begin(), kMarketFields.end());
                    detected = true;
                }
                // raw + derived + market
                if (nScaler == nRaw + nDerived + nMarket) {
                    m_rawFieldCount = nRaw;
                    m_marketFeatureCount = static_cast<int>(nMarket);
                    m_fields.insert(m_fields.end(), kDerivedFields.begin(), kDerivedFields.end());
                    m_fields.insert(m_fields.end(), kMarketFields.begin(), kMarketFields.end());
                    detected = true;
                }

                if (detected) {
                    INTERNAL_INFO_STREAM << "[FeatureTensorBuilder] 特征配置: "
                        << nRaw << " raw";
                    if (m_rawFieldCount > 0) INTERNAL_INFO_STREAM << " + " << nDerived << " derived";
                    if (m_marketFeatureCount > 0) INTERNAL_INFO_STREAM << " + " << nMarket << " market";
                    INTERNAL_INFO_STREAM << " = " << nScaler << " total";
                } else {
                    INTERNAL_ERROR_STREAM << "[FeatureTensorBuilder] scaler维度("
                        << nScaler << ") 无法匹配已知特征组合, 拒绝使用";
                    m_hasScaler = false;
                }
            }
        }
    }
    if (!m_hasScaler) {
        INTERNAL_ERROR_STREAM << "[FeatureTensorBuilder] 无有效 scaler, 推理结果不可用";
    }
}

bool FeatureTensorBuilder::loadScaler(const std::string& path) {
    try {
        auto json = foundation::json::JsonFacade::parseFile(path);
        if (json.isNull() || !json.isObject()) return false;

        auto loadArray = [&](const char* key, std::vector<double>& out) {
            if (!json.has(key)) return false;
            auto arr = json.get(key);
            if (!arr.isArray()) return false;
            for (size_t i = 0; i < arr.size(); ++i) {
                out.push_back(arr.at(i).asDouble());
            }
            return !out.empty();
        };

        m_scalerMeans.clear();
        m_scalerScales.clear();
        m_winsorLo.clear();
        m_winsorHi.clear();
        if (!loadArray("mean", m_scalerMeans)) return false;
        if (!loadArray("scale", m_scalerScales)) return false;
        // winsor_lo/hi 为可选字段，旧模型可能没有
        loadArray("winsor_lo", m_winsorLo);
        loadArray("winsor_hi", m_winsorHi);
        return !m_scalerMeans.empty() && m_scalerMeans.size() == m_scalerScales.size();
    } catch (const std::exception& e) {
        INTERNAL_WARN_STREAM << "[FeatureTensorBuilder] scaler 加载失败: " << e.what();
        return false;
    }
}

std::vector<double> FeatureTensorBuilder::scalerNormalize(
    const std::vector<double>& series, int featureIdx) const
{
    if (!m_hasScaler || featureIdx >= static_cast<int>(m_scalerMeans.size()))
        return zscore(series);

    const double mean = m_scalerMeans[static_cast<size_t>(featureIdx)];
    const double scale = m_scalerScales[static_cast<size_t>(featureIdx)] + 1e-12;
    const bool hasWinsor = featureIdx < static_cast<int>(m_winsorLo.size())
                        && featureIdx < static_cast<int>(m_winsorHi.size());
    const double lo = hasWinsor ? m_winsorLo[static_cast<size_t>(featureIdx)] : -std::numeric_limits<double>::infinity();
    const double hi = hasWinsor ? m_winsorHi[static_cast<size_t>(featureIdx)] : std::numeric_limits<double>::infinity();

    std::vector<double> result;
    result.reserve(series.size());
    for (auto v : series) {
        if (!std::isfinite(v)) { result.push_back(0.0); continue; }
        if (v < lo) v = lo; else if (v > hi) v = hi;
        result.push_back((v - mean) / scale);
    }
    return result;
}

std::vector<std::vector<double>> FeatureTensorBuilder::addDerivedFeatures(
    const std::vector<std::vector<double>>& rawSeries,
    const std::vector<std::string>& rawFields,
    int nTimeSteps)
{
    const int ci = fieldIndex(rawFields, "close");
    const int vi = fieldIndex(rawFields, "volume");
    const int ti = fieldIndex(rawFields, "turnover_rate");

    const int nDerived = static_cast<int>(kDerivedFields.size());
    std::vector<std::vector<double>> derived(nDerived, std::vector<double>(nTimeSteps, 0.0));

    // 日收益率 (vol_5d 需要)
    std::vector<double> ret(nTimeSteps, 0.0);
    for (int t = 1; t < nTimeSteps; ++t) {
        double prev = rawSeries[ci][t - 1];
        double cur = rawSeries[ci][t];
        ret[t] = (prev > 1e-9) ? (cur / prev - 1.0) : 0.0;
    }

    // ret_5d
    for (int t = 5; t < nTimeSteps; ++t) {
        double prev = rawSeries[ci][t - 5];
        derived[0][t] = (prev > 1e-9) ? (rawSeries[ci][t] / prev - 1.0) : 0.0;
    }
    // ret_20d
    for (int t = 20; t < nTimeSteps; ++t) {
        double prev = rawSeries[ci][t - 20];
        derived[1][t] = (prev > 1e-9) ? (rawSeries[ci][t] / prev - 1.0) : 0.0;
    }
    // vol_5d
    for (int t = 5; t < nTimeSteps; ++t) {
        double sum2 = 0.0;
        for (int j = t - 4; j <= t; ++j) sum2 += ret[j] * ret[j];
        derived[2][t] = std::sqrt(sum2 / 5.0);
    }
    // ma10_dev
    for (int t = 10; t < nTimeSteps; ++t) {
        double sum = 0.0;
        for (int j = t - 9; j <= t; ++j) sum += rawSeries[ci][j];
        double ma = sum / 10.0;
        derived[3][t] = (ma > 1e-9) ? (rawSeries[ci][t] / ma - 1.0) : 0.0;
    }
    // vol_ratio_5d
    for (int t = 5; t < nTimeSteps; ++t) {
        double prev = rawSeries[vi][t - 5];
        derived[4][t] = (prev > 1e-9) ? (rawSeries[vi][t] / prev - 1.0) : 0.0;
    }
    // turnover_chg
    for (int t = 5; t < nTimeSteps; ++t) {
        double prev = rawSeries[ti][t - 5];
        derived[5][t] = (prev > 1e-9) ? (rawSeries[ti][t] / prev - 1.0) : 0.0;
    }

    for (auto& col : derived)
        for (auto& v : col)
            if (v > 10.0) v = 10.0; else if (v < -10.0) v = -10.0;

    return derived;
}

FeatureTensor FeatureTensorBuilder::build(
    const factor::HistoricalView& view,
    const std::vector<std::string>& symbols,
    const std::string& anchorDate)
{
    FeatureTensor result;
    result.featureCount = static_cast<int>(m_fields.size());
    result.lookbackWindow = m_lookbackWindow;

    const int N = static_cast<int>(symbols.size());
    const int F = static_cast<int>(m_fields.size());
    const int W = m_lookbackWindow;

    // 拉取扩展窗口以支持可靠的 ffill（训练侧使用全历史 ffill）。
    // 5×W 覆盖绝大多数停牌场景（A股最长连续停牌通常 < 60 个交易日）。
    constexpr int kExtendedFactor = 5;
    const int extendedWindow = W * kExtendedFactor + 1;  // +1 用于丢弃 anchorDate

    // LSTM 期望 [N, W, F] (batch × seq_len × features)
    result.shape = {N, W, F};
    result.data.resize(static_cast<size_t>(N) * W * F, 0.0f);

    int validSymbols = 0;

    // Fraw: 需要从数据源加载的字段数 (排除市场特征, 它们在内存中计算)
    const int nMktF = m_marketFeatureCount;
    const int Fdata = F - nMktF;  // raw + derived (不含 market)
    const int Fraw = (m_rawFieldCount > 0) ? static_cast<int>(m_rawFieldCount) : Fdata;
    const bool hasDerived = (m_rawFieldCount > 0);
    const bool hasMarket = (nMktF > 0);
    const int ci = fieldIndex(m_fields, "close");
    const int ii = fieldIndex(m_fields, "industry_code");

    // ═══════════════════════════════════════════
    // 两轮扫描路径: 有市场特征时
    // Pass 1: 构建所有标的的原始窗口 + 保留 prevClose/industry
    // Cross-sectional: 按窗口日期计算市场统计量
    // Pass 2: 拼接市场特征 + scaler 归一化
    // ═══════════════════════════════════════════
    if (hasMarket) {
        struct RawWindow {
            std::vector<std::vector<double>> series;  // [Fraw][W]
            double prevClose;  // close 在窗口前一天的值
            int industryCode;
            bool valid;
        };
        std::vector<RawWindow> rawStocks(N);

        // Pass 1: 构建所有标的的原始窗口
        for (int n = 0; n < N; ++n) {
            auto& rw = rawStocks[n];
            rw.series.resize(Fraw);
            std::vector<std::vector<double>> rawExtended(Fraw);
            int extLen = 0;
            bool ok = true;

            for (int f = 0; f < Fraw; ++f) {
                auto series = view.getSeries(symbols[static_cast<size_t>(n)],
                                              anchorDate, extendedWindow,
                                              m_fields[static_cast<size_t>(f)],
                                              /*includeNaN=*/true);
                const int rawLen = static_cast<int>(series.size());
                if (f == 0) extLen = rawLen;
                if (rawLen < W + 1 || rawLen != extLen) { ok = false; break; }

                std::vector<double> values(static_cast<size_t>(rawLen));
                for (int si = 0; si < rawLen; ++si)
                    values[static_cast<size_t>(si)] = series[static_cast<size_t>(si)].value;
                for (int vi = 0; vi < rawLen; ++vi)
                    if (!std::isfinite(values[static_cast<size_t>(vi)]) && vi > 0
                        && std::isfinite(values[static_cast<size_t>(vi - 1)]))
                        values[static_cast<size_t>(vi)] = values[static_cast<size_t>(vi - 1)];
                rawExtended[static_cast<size_t>(f)] = std::move(values);
            }
            if (!ok || extLen < W + 2) {
                rw.valid = false; continue;
            }

            // 切片到 W 天窗口: [anchorDate-W : anchorDate-1]
            const int tailStart = extLen - (W + 1);
            for (int f = 0; f < Fraw; ++f) {
                auto& col = rawExtended[static_cast<size_t>(f)];
                rw.series[f].reserve(W);
                for (int wi = 0; wi < W; ++wi)
                    rw.series[f].push_back(col[static_cast<size_t>(tailStart + wi)]);
            }

            // 保留窗口前一日的 close (用于计算首日收益率)
            rw.prevClose = rawExtended[static_cast<size_t>(ci)][static_cast<size_t>(tailStart - 1)];

            // 行业编码: 取窗口中最后一个有效值
            rw.industryCode = 0;
            for (int wi = W - 1; wi >= 0; --wi) {
                double ind = rawExtended[static_cast<size_t>(ii)][static_cast<size_t>(tailStart + wi)];
                if (std::isfinite(ind)) { rw.industryCode = static_cast<int>(ind); break; }
            }

            // 窗口有效性检查
            bool allFinite = true;
            for (int f = 0; f < Fraw && allFinite; ++f)
                for (int wi = 0; wi < W && allFinite; ++wi)
                    if (!std::isfinite(rw.series[f][static_cast<size_t>(wi)])) allFinite = false;
            rw.valid = allFinite;
        }

        // Cross-sectional: 按窗口日期 (0..W-1) 计算市场统计量
        const int nMkt = m_marketFeatureCount;
        // marketStats[n][w] = {mkt_ret, breadth, vol}[3], industryRel[per stock]
        // 直接写入 market window: per-symbol, [nMkt][W]
        std::vector<std::vector<std::vector<double>>> mktWindows(
            N, std::vector<std::vector<double>>(nMkt, std::vector<double>(W, 0.0)));

        for (int w = 0; w < W; ++w) {
            // 收集该窗口日所有有效标的的收益率和行业
            std::vector<double> rets; rets.reserve(N);
            std::vector<int> stockIndices; stockIndices.reserve(N);
            std::vector<int> industries; industries.reserve(N);

            for (int n = 0; n < N; ++n) {
                if (!rawStocks[n].valid) continue;
                double c = rawStocks[n].series[ci][static_cast<size_t>(w)];
                double prev = (w == 0) ? rawStocks[n].prevClose
                                       : rawStocks[n].series[ci][static_cast<size_t>(w - 1)];
                if (prev > 1e-9 && c > 1e-9) {
                    double r = c / prev - 1.0;
                    if (r > -0.2 && r < 0.2) {  // 单日收益限幅
                        rets.push_back(r);
                        stockIndices.push_back(n);
                        industries.push_back(rawStocks[n].industryCode);
                    }
                }
            }

            const size_t nv = rets.size();
            if (nv < 10) continue;

            // 市场统计
            double sum = 0.0, posSum = 0.0;
            for (double r : rets) { sum += r; if (r > 0) posSum += 1.0; }
            double mktRet = sum / static_cast<double>(nv);
            double mktBreadth = posSum / static_cast<double>(nv);

            double sumSq = 0.0;
            for (double r : rets) { double d = r - mktRet; sumSq += d * d; }
            double mktVol = std::sqrt(sumSq / static_cast<double>(nv));

            // 行业均值
            std::unordered_map<int, std::pair<double, int>> indAcc;  // {sum, count}
            for (size_t i = 0; i < nv; ++i) {
                auto& acc = indAcc[industries[i]];
                acc.first += rets[i]; acc.second++;
            }
            std::unordered_map<int, double> indAvg;
            for (auto& [ind, acc] : indAcc)
                if (acc.second >= 3) indAvg[ind] = acc.first / acc.second;

            // 写入市场特征
            for (size_t i = 0; i < nv; ++i) {
                int n = stockIndices[i];
                double indRel = 0.0;
                auto it = indAvg.find(industries[i]);
                if (it != indAvg.end()) indRel = rets[i] - it->second;
                mktWindows[n][0][static_cast<size_t>(w)] = mktRet;
                mktWindows[n][1][static_cast<size_t>(w)] = mktBreadth;
                mktWindows[n][2][static_cast<size_t>(w)] = mktVol;
                mktWindows[n][3][static_cast<size_t>(w)] = indRel;
            }
        }

        // Pass 2: 拼接 raw + market + scaler_norm + 写入张量
        for (int n = 0; n < N; ++n) {
            if (!rawStocks[n].valid) continue;

            // 组装 allSeries: raw + market
            std::vector<std::vector<double>> allSeries(F);
            for (int f = 0; f < Fraw; ++f)
                allSeries[f] = rawStocks[n].series[f];
            for (int m = 0; m < nMkt; ++m)
                allSeries[Fraw + m] = std::move(mktWindows[n][m]);

            // scaler 归一化
            for (int f = 0; f < F; ++f)
                allSeries[f] = scalerNormalize(allSeries[f], f);

            // 写入张量 [N, W, F]
            for (int w = 0; w < W; ++w) {
                for (int f = 0; f < F; ++f) {
                    const size_t idx = (static_cast<size_t>(validSymbols) * static_cast<size_t>(W)
                                        + static_cast<size_t>(w)) * static_cast<size_t>(F)
                                        + static_cast<size_t>(f);
                    result.data[idx] = static_cast<float>(allSeries[static_cast<size_t>(f)][static_cast<size_t>(w)]);
                }
            }
            result.symbols.push_back(symbols[static_cast<size_t>(n)]);
            ++validSymbols;
        }
    } else {
    // ═══════════════════════════════════════════
    // 单轮扫描路径: 无市场特征 (backward compatible)
    // ═══════════════════════════════════════════

    for (int n = 0; n < N; ++n) {
        // Step 1: 加载原始字段 + ffill + 计算派生特征 (全部在扩展窗口上)
        std::vector<std::vector<double>> rawExtended(Fraw);
        int extLen = 0;
        bool hasAllFields = true;

        for (int f = 0; f < Fraw; ++f) {
            auto series = view.getSeries(symbols[static_cast<size_t>(n)],
                                          anchorDate, extendedWindow,
                                          m_fields[static_cast<size_t>(f)],
                                          /*includeNaN=*/true);

            const int rawLen = static_cast<int>(series.size());
            if (f == 0) { extLen = rawLen; }
            if (rawLen < W + 1 || rawLen != extLen) { hasAllFields = false; break; }

            std::vector<double> values(static_cast<size_t>(rawLen));
            for (int si = 0; si < rawLen; ++si)
                values[static_cast<size_t>(si)] = series[static_cast<size_t>(si)].value;

            // ffill
            for (int vi = 0; vi < rawLen; ++vi) {
                if (!std::isfinite(values[static_cast<size_t>(vi)]) && vi > 0
                    && std::isfinite(values[static_cast<size_t>(vi - 1)]))
                    values[static_cast<size_t>(vi)] = values[static_cast<size_t>(vi - 1)];
            }
            rawExtended[static_cast<size_t>(f)] = std::move(values);
        }
        if (!hasAllFields) continue;

        // Step 2: 从扩展窗口计算派生特征 (与训练一致: 全历史算派生 → 切片)
        std::vector<std::string> rawFieldNames(m_fields.begin(), m_fields.begin() + Fraw);
        std::vector<std::vector<double>> allExtended;
        if (hasDerived) {
            auto derived = addDerivedFeatures(rawExtended, rawFieldNames, extLen);
            allExtended.reserve(F);
            for (int f = 0; f < Fraw; ++f)
                allExtended.push_back(std::move(rawExtended[f]));
            for (auto& d : derived)
                allExtended.push_back(std::move(d));
        } else {
            allExtended = std::move(rawExtended);
        }

        // Step 3: 切片到 W 天窗口 (丢弃 anchorDate): [anchorDate-W : anchorDate-1]
        const int tailStart = extLen - (W + 1);
        std::vector<std::vector<double>> allSeries(F);
        for (int f = 0; f < F; ++f) {
            auto& col = allExtended[static_cast<size_t>(f)];
            allSeries[f].reserve(W);
            for (int wi = 0; wi < W; ++wi)
                allSeries[f].push_back(col[static_cast<size_t>(tailStart + wi)]);
            if (!std::all_of(allSeries[f].begin(), allSeries[f].end(),
                    [](double v) { return std::isfinite(v); })) {
                hasAllFields = false; break;
            }
        }
        if (!hasAllFields) continue;

        // Step 4: scaler 归一化 + 写入张量
        for (int f = 0; f < F; ++f)
            allSeries[f] = scalerNormalize(allSeries[f], f);

        for (int w = 0; w < W; ++w) {
            for (int f = 0; f < F; ++f) {
                const size_t idx = (static_cast<size_t>(n) * static_cast<size_t>(W) + static_cast<size_t>(w)) * static_cast<size_t>(F) + static_cast<size_t>(f);
                result.data[idx] = static_cast<float>(allSeries[static_cast<size_t>(f)][static_cast<size_t>(w)]);
            }
        }

        result.symbols.push_back(symbols[static_cast<size_t>(n)]);
        ++validSymbols;
    }
    }  // else (无市场特征单轮路径)

    if (validSymbols > 0) {
        result.valid = true;
        // 首次成功构建时输出诊断: 第一个标的每个特征的均值/标准差
        static bool s_builderDiag = true;
        if (s_builderDiag) {
            s_builderDiag = false;
            std::string diag;
            for (int f = 0; f < F; ++f) {
                double fs = 0.0, fs2 = 0.0;
                int fcnt = 0;
                for (int w = 0; w < W; ++w) {
                    const size_t idx = static_cast<size_t>(w) * static_cast<size_t>(F) + static_cast<size_t>(f);
                    double v = result.data[idx];
                    fs += v; fs2 += v * v; ++fcnt;
                }
                if (fcnt > 0) {
                    double fm = fs / fcnt;
                    double fstd = std::sqrt(fs2 / fcnt - fm * fm);
                    char buf[128];
                    std::snprintf(buf, sizeof(buf),
                        "\n  [%d] %s: mean=%.3f std=%.3f", f, m_fields[static_cast<size_t>(f)].c_str(), fm, fstd);
                    diag += buf;
                }
            }
            INTERNAL_INFO_STREAM << "[FeatureTensorBuilder] 首次构建完成: validSymbols="
                << validSymbols << " W=" << W << " F=" << F
                << " hasScaler=" << (m_hasScaler ? "true" : "false") << diag;
        }
    }

    return result;
}

std::vector<double> FeatureTensorBuilder::zscore(const std::vector<double>& series)
{
    if (series.empty()) return {};

    double mean = 0.0;
    int valid = 0;
    for (auto v : series) {
        if (std::isfinite(v)) { mean += v; ++valid; }
    }
    if (valid == 0) return std::vector<double>(series.size(), 0.0);
    mean /= valid;

    double var = 0.0;
    for (auto v : series) {
        if (std::isfinite(v)) var += (v - mean) * (v - mean);
    }
    var /= valid;

    std::vector<double> result;
    result.reserve(series.size());
    const double denom = std::sqrt(var) + 1e-12;
    for (auto v : series) {
        result.push_back(std::isfinite(v) ? (v - mean) / denom : 0.0);
    }
    return result;
}

} // namespace factor::compute