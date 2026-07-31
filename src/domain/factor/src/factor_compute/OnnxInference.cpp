#include "factor_compute/IModelInference.h"

#ifdef HAS_ONNX_RUNTIME
#include <onnxruntime_cxx_api.h>
#endif

#include "foundation/log/logging.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace factor::compute {
namespace {

#ifdef HAS_ONNX_RUNTIME

/// 进程级全局 ONNX Runtime 环境 (线程安全, 所有 DLFactor 实例共享)
static Ort::Env& globalEnv() {
    static Ort::Env s_env(ORT_LOGGING_LEVEL_WARNING, "DLFactor");
    return s_env;
}

/// ONNX Runtime 实现
class OnnxInference final : public IModelInference {
public:
    OnnxInference() = default;

    bool loadModel(const std::string& modelPath) override {
        if (!std::filesystem::exists(modelPath)) {
            INTERNAL_ERROR_STREAM << "[OnnxInference] 模型文件不存在: " << modelPath;
            return false;
        }

        try {
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads(1);
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);

            auto& env = globalEnv();
            std::wstring wpath(modelPath.begin(), modelPath.end());
            m_session = std::make_unique<Ort::Session>(env, wpath.c_str(), opts);
            m_modelPath = modelPath;

            // 获取输入输出信息
            const size_t nInputs = m_session->GetInputCount();
            m_inputNames.resize(nInputs);
            m_inputShapes.resize(nInputs);
            Ort::AllocatorWithDefaultOptions alloc;
            for (size_t i = 0; i < nInputs; ++i) {
                auto name = m_session->GetInputNameAllocated(i, alloc);
                m_inputNames[i] = name.get() ? std::string(name.get()) : "";
                auto typeInfo = m_session->GetInputTypeInfo(i);
                auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
                m_inputShapes[i] = tensorInfo.GetShape();
            }

            INTERNAL_INFO_STREAM << "[OnnxInference] 模型加载成功: " << modelPath
                << " inputs=" << nInputs;
            return true;
        } catch (const std::exception& e) {
            INTERNAL_ERROR_STREAM << "[OnnxInference] 加载失败: " << e.what();
            m_session.reset();
            return false;
        }
    }

    std::vector<float> predict(const std::vector<float>& input,
                                const std::vector<int64_t>& shape) override {
        if (!m_session) return {};

        try {
            Ort::AllocatorWithDefaultOptions alloc;
            auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

            // 创建输入张量
            auto inputTensor = Ort::Value::CreateTensor<float>(
                memoryInfo,
                const_cast<float*>(input.data()),
                input.size(),
                shape.data(),
                shape.size());

            std::vector<const char*> inputNamePtrs;
            for (const auto& n : m_inputNames)
                inputNamePtrs.push_back(n.c_str());

            // 获取输出名称
            const size_t nOutputs = m_session->GetOutputCount();
            std::vector<const char*> outputNamePtrs(nOutputs);
            std::vector<std::string> outputNames(nOutputs);
            for (size_t i = 0; i < nOutputs; ++i) {
                auto name = m_session->GetOutputNameAllocated(i, alloc);
                outputNames[i] = name.get() ? std::string(name.get()) : "";
                outputNamePtrs[i] = outputNames[i].c_str();
            }

            // 推理
            auto outputs = m_session->Run(Ort::RunOptions{nullptr},
                inputNamePtrs.data(), &inputTensor, 1,
                outputNamePtrs.data(), nOutputs);

            // 提取结果
            if (outputs.empty()) return {};
            auto& outputTensor = outputs.front();
            auto* data = outputTensor.GetTensorMutableData<float>();
            auto outShape = outputTensor.GetTensorTypeAndShapeInfo().GetShape();
            size_t total = 1;
            for (auto d : outShape) if (d > 0) total *= static_cast<size_t>(d);

            std::vector<float> result(data, data + total);
            return result;

        } catch (const std::exception& e) {
            INTERNAL_ERROR_STREAM << "[OnnxInference] 推理失败: " << e.what();
            return {};
        }
    }

    bool isLoaded() const override { return m_session != nullptr; }
    std::string modelPath() const override { return m_modelPath; }

private:
    std::unique_ptr<Ort::Session> m_session;
    std::string m_modelPath;
    std::vector<std::string> m_inputNames;
    std::vector<std::vector<int64_t>> m_inputShapes;
};

#else // !HAS_ONNX_RUNTIME

/// 无 ONNX 时的空实现
class NoOpInference final : public IModelInference {
public:
    bool loadModel(const std::string&) override {
        INTERNAL_WARN_STREAM << "[NoOpInference] ONNX Runtime 未集成，无法加载模型";
        return false;
    }
    std::vector<float> predict(const std::vector<float>&, const std::vector<int64_t>&) override {
        return {};
    }
    bool isLoaded() const override { return false; }
    std::string modelPath() const override { return {}; }
};

#endif

} // namespace

std::unique_ptr<IModelInference> createInferenceEngine() {
#ifdef HAS_ONNX_RUNTIME
    return std::make_unique<OnnxInference>();
#else
    return std::make_unique<NoOpInference>();
#endif
}

} // namespace factor::compute
