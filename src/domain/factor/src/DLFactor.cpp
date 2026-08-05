#include "domain/factor/include/DLFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include "factor_compute/IModelInference.h"
#include "factor_compute/FeatureTensorBuilder.h"
#include "foundation/log/logging.hpp"

#include <filesystem>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
/// 返回 exe 所在目录 (跨平台, 一次解析后缓存)
const std::string& exeDir() {
    static std::string s_dir = []() {
        std::filesystem::path exePath;
#ifdef _WIN32
        wchar_t buf[MAX_PATH];
        DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (len > 0 && len < MAX_PATH) exePath = std::wstring(buf, len);
#else
        char buf[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0) { buf[len] = '\0'; exePath = buf; }
#endif
        return exePath.parent_path().string();
    }();
    return s_dir;
}

/// 解析模型路径：相对路径 → exe目录/相对路径；绝对路径 → 原样返回
std::string resolveModelPath(const std::string& modelPath) {
    std::filesystem::path p(modelPath);
    if (p.is_absolute()) return modelPath;
    return (std::filesystem::path(exeDir()) / p).string();
}
} // namespace

namespace factor {

DLFactor::DLFactor()
{
    factorType_ = FactorType::DL;
}

CalculationResult DLFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context, "AI因子需要 HistoricalView");
    }

    // ── 模型路径检查 ──
    if (params_.modelPath.empty()) {
        CalculationResult result;
        result.date = context.date;
        result.dataStatus.availability = DataAvailability::UNAVAILABLE;
        result.dataStatus.message = "modelPath 未配置";
        result.metadata.set("error", json_helper::toJsonValue("modelPath empty"));
        result.metadata.set("emptyReason", json_helper::toJsonValue("modelPath empty"));
        return result;
    }

    const CommonParams& common = params_;
    const auto symbols = effectiveSymbols(context);

    return executeWithCommonParams(
        context,
        common,
        [&]() { return context.date; },
        [&](const CommonRuntimeState& runtime, CalculationResult& result) {
            // ── 懒加载模型 (尝试一次, 失败不反复重试) ──
            static std::mutex s_modelMutex;
            static std::unique_ptr<factor::compute::IModelInference> s_engine;
            static std::string s_loadedPath;
            static bool s_loadAttempted = false;
            static int64_t s_lastCheckSec = 0;
            static std::filesystem::file_time_type s_modelMtime;

            auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            // 首次加载 或 每 120s 重试热更新
            bool shouldTry = !s_loadAttempted ||
                (s_engine && s_engine->isLoaded() && (nowSec - s_lastCheckSec) >= 120);

            if (shouldTry) {
                std::lock_guard<std::mutex> lock(s_modelMutex);
                s_lastCheckSec = nowSec;

                // 解析路径: 相对路径基于 exe 所在目录
                std::string resolvedPath = resolveModelPath(params_.modelPath);
                std::filesystem::path absPath(resolvedPath);

                if (!s_engine || !s_engine->isLoaded() || s_loadedPath != resolvedPath) {
                    auto engine = factor::compute::createInferenceEngine();
                    if (engine->loadModel(resolvedPath)) {
                        s_engine = std::move(engine);
                        s_loadedPath = resolvedPath;
                        s_modelMtime = std::filesystem::last_write_time(absPath);
                        INTERNAL_INFO_STREAM << "[DLFactor] 模型加载成功: " << resolvedPath;
                    } else if (!s_loadAttempted) {
                        INTERNAL_ERROR_STREAM << "[DLFactor] 模型加载失败: " << resolvedPath;
                    }
                }
                s_loadAttempted = true;
            }

            // 已加载时检测目录变化(热更新)
            if (s_engine && s_engine->isLoaded() && (nowSec - s_lastCheckSec) >= 120) {
                std::filesystem::path modelDir =
                    std::filesystem::absolute(params_.modelPath).parent_path();
                if (std::filesystem::exists(modelDir)) {
                    auto latestMtime = std::filesystem::last_write_time(modelDir);
                    for (const auto& e : std::filesystem::directory_iterator(modelDir)) {
                        auto t = std::filesystem::last_write_time(e);
                        if (t > latestMtime) latestMtime = t;
                    }
                    if (latestMtime != s_modelMtime) {
                        auto engine = factor::compute::createInferenceEngine();
                        if (engine->loadModel(s_loadedPath)) {
                            s_engine = std::move(engine);
                            s_modelMtime = latestMtime;
                        }
                    }
                }
            }

            if (!s_engine || !s_engine->isLoaded()) {
                for (const auto& symbol : symbols)
                    result.values[symbol] = 0.0;
                result.metadata.set("inferenceAvailable",
                    json_helper::toJsonValue(false));
                result.metadata.set("emptyReason",
                    json_helper::toJsonValue("model unavailable"));
                return;
            }

            // ── 构建特征张量 ──
            auto fields = getDataRequirements().requiredFields;
            factor::compute::FeatureTensorBuilder builder(
                fields, params_.lookbackWindow, s_loadedPath);

            auto tensor = builder.build(*context.historicalView,
                symbols, runtime.effectiveDate);

            if (!tensor.valid) {
                result.dataStatus.message = "无法构建有效特征张量";
                result.metadata.set("emptyReason",
                    json_helper::toJsonValue("no valid features"));
                result.metadata.set("tensorSymbols",
                    json_helper::toJsonValue(static_cast<int>(tensor.symbols.size())));
                return;
            }

            // ── 推理 ──
            auto outputs = s_engine->predict(tensor.data, tensor.shape);
            if (outputs.empty()) {
                result.metadata.set("emptyReason",
                    json_helper::toJsonValue("predict returned empty"));
                result.metadata.set("tensorShape",
                    json_helper::toJsonValue(static_cast<int>(tensor.data.size())));
                return;
            }

            // ── 映射结果 ──
            for (size_t i = 0; i < tensor.symbols.size() && i < outputs.size(); ++i) {
                if (std::isfinite(outputs[i]))
                    result.values[tensor.symbols[i]] = static_cast<double>(outputs[i]);
            }

            result.metadata.set("inferenceAvailable", json_helper::toJsonValue(true));
            result.metadata.set("modelPath",
                json_helper::toJsonValue(s_loadedPath));
            result.metadata.set("validSymbols",
                json_helper::toJsonValue(static_cast<int>(tensor.symbols.size())));

            static bool s_diagOnce = true;
            if (s_diagOnce && tensor.valid) {
                s_diagOnce = false;
                // 张量统计 (验证 scaler 归一化是否正常)
                double tmin = tensor.data[0], tmax = tensor.data[0];
                double tsum = 0.0;
                size_t tn = 0;
                for (auto v : tensor.data) { if (v < tmin) tmin = v; if (v > tmax) tmax = v; tsum += v; ++tn; }
                double tmean = tn > 0 ? tsum / tn : 0.0;
                // 输出统计
                double omin = outputs[0], omax = outputs[0], osum = 0.0;
                for (auto v : outputs) { if (v < omin) omin = v; if (v > omax) omax = v; osum += v; }
                double omean = outputs.size() > 0 ? osum / outputs.size() : 0.0;
                INTERNAL_ERROR_STREAM << "[DLFactor] TENSOR[" << tensor.data.size() << " elems] "
                    << "min=" << tmin << " max=" << tmax << " mean=" << tmean
                    << " | OUTPUT[N=" << outputs.size() << "] "
                    << "min=" << omin << " max=" << omax << " mean=" << omean
                    << " | validSymbols=" << tensor.symbols.size()
                    << " hasScaler=" << builder.hasScaler()
                    << " scalerPath=" << s_loadedPath;
            }
        },
        [](const CommonRuntimeState&, CalculationResult&) {},
        [&](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("modelType",
                json_helper::toJsonValue(static_cast<int>(params_.modelType)));
            result.metadata.set("featureCount",
                json_helper::toJsonValue(params_.featureCount));
            result.metadata.set("predictionHorizon",
                json_helper::toJsonValue(params_.predictionHorizon));
        });
}

std::shared_ptr<DLFactor> DLFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<DLFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

DataRequirements DLFactor::getDataRequirements() const
{
    DataRequirements req;
    // 字段顺序必须与 train.py FEATURE_FIELDS 严格一致
    appendRequiredField(req, "close");
    appendRequiredField(req, "open");
    appendRequiredField(req, "high");
    appendRequiredField(req, "low");
    appendRequiredField(req, "volume");
    appendRequiredField(req, "turnover_rate");
    appendRequiredField(req, "amplitude");
    appendRequiredField(req, "pe_ratio");
    appendRequiredField(req, "pb_ratio");
    appendRequiredField(req, "market_cap");
    appendRequiredField(req, "roe");
    appendRequiredField(req, "industry_code");
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules DLFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, static_cast<int>(params_.lookbackWindow));
    return rules;
}

void DLFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config::hasCalculationConfig(config))
        params_.fromJson(config::calculationConfig(config));
    dataRequirements_ = getDataRequirements();
}

} // namespace factor
