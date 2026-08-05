#pragma once
// RulePatchLoader — 加密规则补丁加载器
// 职责: 解密 .rulepatch 文件 → 验签 → 生成操作列表 → 应用到 RuleLibrary
// 纯 C++17, 零 Qt 依赖

#include "RuleTypes.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace domain::strategy::rules {

/// @brief 补丁操作
struct PatchOperation {
    enum class Op : std::uint8_t { Add, Modify, Disable, Enable };

    Op op{Op::Add};
    std::string ruleId;
    std::string templateId;
    std::string templateJson;
    std::string fieldsJson;
};

#pragma pack(push, 1)
/// @brief 补丁头部 (64 bytes, 明文, 紧凑布局)
struct PatchHeader {
    static constexpr std::uint32_t kMagic = 0x50525141;
    static constexpr std::uint32_t kCurrentVersion = 1;
    static constexpr std::size_t   kHeaderSize = 64;

    std::uint32_t magic{kMagic};
    std::uint32_t version{kCurrentVersion};
    std::uint32_t patchVersion{0};
    std::uint32_t payloadLen{0};
    std::uint8_t  iv[12]{};
    std::uint8_t  reserved[36]{};  // 4+4+4+4+12+36 = 64 bytes

    [[nodiscard]] bool isValid() const noexcept {
        return magic == kMagic && version == kCurrentVersion && payloadLen > 0;
    }
};
#pragma pack(pop)
static_assert(sizeof(PatchHeader) == 64, "PatchHeader must be 64 bytes");

/// @brief 加密/签名接口 (C++17, 无 std::span)
class ICryptoProvider {
public:
    virtual ~ICryptoProvider() = default;

    [[nodiscard]] virtual std::string decrypt(
        const std::uint8_t* ciphertext, std::size_t cipherLen,
        const std::uint8_t* iv, std::size_t ivLen,
        const std::uint8_t* tag, std::size_t tagLen) const = 0;

    [[nodiscard]] virtual bool verifyHmac(
        const std::uint8_t* data, std::size_t dataLen,
        const std::uint8_t* signature, std::size_t sigLen) const = 0;

    [[nodiscard]] virtual std::string sha256(
        const std::uint8_t* data, std::size_t dataLen) const = 0;
};

/// @brief 加密规则补丁加载器
class RulePatchLoader {
public:
    explicit RulePatchLoader(const ICryptoProvider& crypto);

    /// @brief 从文件加载并验证补丁 (修改内部状态 — 非 const)
    [[nodiscard]] std::vector<PatchOperation> loadFromFile(
        const std::string& filePath);

    /// @brief 将补丁操作应用到规则库
    int apply(const std::vector<PatchOperation>& ops, RuleLibrary& library) const;

    [[nodiscard]] std::uint32_t loadedPatchVersion() const noexcept {
        return m_loadedPatchVersion;
    }
    [[nodiscard]] const std::string& loadedPatchHash() const noexcept {
        return m_loadedPatchHash;
    }

private:
    [[nodiscard]] static PatchHeader parseHeader(const std::uint8_t* raw);

    [[nodiscard]] static std::vector<PatchOperation> parseOperations(
        const std::string& jsonStr);

    const ICryptoProvider& m_crypto;
    std::uint32_t m_loadedPatchVersion{0};
    std::string m_loadedPatchHash;
};

} // namespace domain::strategy::rules
