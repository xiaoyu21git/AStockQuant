#pragma once
// IModelInference — AI 推理引擎抽象接口
// DLFactor 通过此接口加载模型并执行前向推理，不绑定具体框架

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace factor::compute {

class IModelInference {
public:
    virtual ~IModelInference() = default;

    /// @brief 加载 ONNX 模型文件
    /// @return true=加载成功, false=文件不存在/格式错误/框架不可用
    virtual bool loadModel(const std::string& modelPath) = 0;

    /// @brief 执行推理
    /// @param input  展平的输入张量 [N*F*W]
    /// @param shape  张量维度 {N, F, W}
    /// @return 输出向量 [N] (每个标的的预测值)
    virtual std::vector<float> predict(const std::vector<float>& input,
                                        const std::vector<int64_t>& shape) = 0;

    /// @brief 模型是否已加载
    virtual bool isLoaded() const = 0;

    /// @brief 获取已加载模型路径
    virtual std::string modelPath() const = 0;
};

/// @brief 工厂函数：创建推理引擎实例（ONNX 可用时返回 OnnxInference，否则 NoOp）
std::unique_ptr<IModelInference> createInferenceEngine();

} // namespace factor::compute
