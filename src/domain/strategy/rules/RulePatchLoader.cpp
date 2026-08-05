// RulePatchLoader — 加密规则补丁加载器实现
#include "RulePatchLoader.h"
#include "RuleLibrary.h"
#include "RuleConditionEvaluator.h"

#include "foundation/json/json_facade.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace domain::strategy::rules {

// ═══════════════════════════════════════════════════════════════════
// RulePatchLoader
// ═══════════════════════════════════════════════════════════════════

RulePatchLoader::RulePatchLoader(const ICryptoProvider& crypto)
    : m_crypto(crypto) {}

PatchHeader RulePatchLoader::parseHeader(const std::uint8_t* raw)
{
    PatchHeader hdr;
    std::memcpy(&hdr, raw, PatchHeader::kHeaderSize);
    return hdr;
}

std::vector<PatchOperation> RulePatchLoader::parseOperations(const std::string& jsonStr)
{
    std::vector<PatchOperation> ops;
    auto root = foundation::json::JsonFacade::parse(jsonStr);
    if (!root.has("operations")) return ops;

    auto arr = root.get("operations");
    for (std::size_t i = 0; i < arr.size(); ++i) {
        auto entry = arr.at(i);
        PatchOperation op;
        std::string opStr = entry.has("op") ? entry.get("op").asString() : "";

        if (opStr == "add") {
            op.op = PatchOperation::Op::Add;
            if (entry.has("template")) {
                op.templateJson = entry.get("template").toString();
                if (entry.get("template").has("templateId"))
                    op.templateId = entry.get("template").get("templateId").asString();
            }
        } else if (opStr == "modify") {
            op.op = PatchOperation::Op::Modify;
            op.ruleId = entry.has("ruleId") ? entry.get("ruleId").asString() : "";
            op.fieldsJson = entry.has("fields") ? entry.get("fields").toString() : "";
        } else if (opStr == "disable") {
            op.op = PatchOperation::Op::Disable;
            op.ruleId = entry.has("ruleId") ? entry.get("ruleId").asString() : "";
        } else if (opStr == "enable") {
            op.op = PatchOperation::Op::Enable;
            op.ruleId = entry.has("ruleId") ? entry.get("ruleId").asString() : "";
        }
        ops.push_back(std::move(op));
    }
    return ops;
}

std::vector<PatchOperation> RulePatchLoader::loadFromFile(
    const std::string& filePath)
{
    // 1. 读取文件
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        INTERNAL_WARN_STREAM << "[PatchLoader] 无法打开补丁文件: " << filePath;
        return {};
    }

    file.seekg(0, std::ios::end);
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (fileSize < static_cast<std::streamsize>(PatchHeader::kHeaderSize + 32)) {
        INTERNAL_WARN_STREAM << "[PatchLoader] 补丁文件太小: " << fileSize << " bytes";
        return {};
    }

    std::vector<std::uint8_t> rawData(static_cast<std::size_t>(fileSize));
    file.read(reinterpret_cast<char*>(rawData.data()), fileSize);

    // 2. 解析头部
    PatchHeader hdr = parseHeader(rawData.data());
    if (!hdr.isValid()) {
        INTERNAL_WARN_STREAM << "[PatchLoader] 无效补丁头部 magic=0x"
                             << std::hex << hdr.magic;
        return {};
    }

    // 3. 分离各部分
    const std::uint8_t* payloadStart = rawData.data() + PatchHeader::kHeaderSize;
    const std::uint8_t* signatureStart = payloadStart + hdr.payloadLen;
    std::size_t totalExpected = PatchHeader::kHeaderSize + hdr.payloadLen + 32;

    if (rawData.size() < totalExpected) {
        INTERNAL_WARN_STREAM << "[PatchLoader] 文件大小不匹配";
        return {};
    }

    // 4. HMAC 验签 (header + ciphertext)
    std::size_t signedLen = PatchHeader::kHeaderSize + hdr.payloadLen;
    if (!m_crypto.verifyHmac(rawData.data(), signedLen, signatureStart, 32)) {
        INTERNAL_WARN_STREAM << "[PatchLoader] HMAC 签名验证失败!";
        return {};
    }

    // 5. AES-GCM 解密 (密文末尾 16 bytes 是 auth tag)
    if (hdr.payloadLen < 16) {
        INTERNAL_WARN_STREAM << "[PatchLoader] 密文太短, 缺 GCM auth tag";
        return {};
    }
    std::size_t cipherLen = hdr.payloadLen - 16;
    std::string plaintext = m_crypto.decrypt(
        payloadStart, cipherLen,
        hdr.iv, 12,
        payloadStart + cipherLen, 16);

    if (plaintext.empty()) {
        INTERNAL_WARN_STREAM << "[PatchLoader] 解密失败!";
        return {};
    }

    // 6. 计算补丁哈希
    m_loadedPatchHash = m_crypto.sha256(rawData.data(), signedLen);
    m_loadedPatchVersion = hdr.patchVersion;

    INTERNAL_INFO_STREAM << "[PatchLoader] 补丁验证通过 v" << hdr.patchVersion
                         << " hash=" << m_loadedPatchHash.substr(0, 16) << "...";

    return parseOperations(plaintext);
}

int RulePatchLoader::apply(const std::vector<PatchOperation>& ops,
                            RuleLibrary& library) const
{
    int applied = 0;
    for (const auto& op : ops) {
        switch (op.op) {
        case PatchOperation::Op::Add:
            INTERNAL_INFO_STREAM << "[PatchLoader] ADD template=" << op.templateId;
            ++applied;
            break;
        case PatchOperation::Op::Modify:
            INTERNAL_INFO_STREAM << "[PatchLoader] MODIFY rule=" << op.ruleId;
            ++applied;
            break;
        case PatchOperation::Op::Disable:
            INTERNAL_INFO_STREAM << "[PatchLoader] DISABLE rule=" << op.ruleId;
            ++applied;
            break;
        case PatchOperation::Op::Enable:
            INTERNAL_INFO_STREAM << "[PatchLoader] ENABLE rule=" << op.ruleId;
            ++applied;
            break;
        }
    }
    return applied;
}

} // namespace domain::strategy::rules
