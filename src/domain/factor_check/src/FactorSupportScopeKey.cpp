#include "factor_check/FactorSupportScopeKey.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

namespace factor::check {

namespace {

constexpr std::array<std::uint32_t, 64> kMd5K = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

constexpr std::array<std::uint32_t, 64> kMd5Shift = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

std::uint32_t leftRotate(std::uint32_t value, std::uint32_t shift)
{
    return (value << shift) | (value >> (32U - shift));
}

std::string bytesToHex(const std::array<std::uint8_t, 16>& bytes)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (const std::uint8_t byte : bytes) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::string trimCopy(const std::string& value)
{
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    auto begin = std::find_if(value.begin(), value.end(), notSpace);
    if (begin == value.end()) {
        return {};
    }
    auto end = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    return std::string(begin, end);
}

std::string normalizeMode(const std::string& value)
{
    std::string result = trimCopy(value);
    for (char& ch : result) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return result.empty() ? std::string("cache") : result;
}

nlohmann::json parseSnapshot(const std::string& snapshot)
{
    if (snapshot.empty()) {
        return nlohmann::json::object();
    }

    try {
        nlohmann::json parsed = nlohmann::json::parse(snapshot);
        if (parsed.is_object()) {
            return parsed;
        }
    } catch (...) {
    }

    return nlohmann::json::object();
}

void normalizeAvailableFields(nlohmann::json& snapshot)
{
    if (!snapshot.is_object()) {
        snapshot = nlohmann::json::object();
        return;
    }

    if (!snapshot.contains("availableFields") || !snapshot["availableFields"].is_array()) {
        return;
    }

    std::vector<std::string> fields;
    fields.reserve(snapshot["availableFields"].size());
    for (const nlohmann::json& field : snapshot["availableFields"]) {
        if (!field.is_string()) {
            continue;
        }

        const std::string normalized = trimCopy(field.get<std::string>());
        if (!normalized.empty()) {
            fields.push_back(normalized);
        }
    }

    std::sort(fields.begin(), fields.end());
    fields.erase(std::unique(fields.begin(), fields.end()), fields.end());

    nlohmann::json normalizedArray = nlohmann::json::array();
    for (const std::string& field : fields) {
        normalizedArray.push_back(field);
    }

    snapshot["availableFields"] = std::move(normalizedArray);
}

} // namespace

std::string buildScopePayloadJson(const std::string& dataSourceMode,
                                  int selectedDatasetId,
                                  const std::string& startDate,
                                  const std::string& endDate,
                                  const std::string& cacheSnapshotJson)
{
    nlohmann::json cacheSnapshot = parseSnapshot(cacheSnapshotJson);
    normalizeAvailableFields(cacheSnapshot);

    nlohmann::json payload = nlohmann::json::object();
    payload["dataSourceMode"] = normalizeMode(dataSourceMode);
    payload["selectedDatasetId"] = selectedDatasetId;
    payload["startDate"] = trimCopy(startDate);
    payload["endDate"] = trimCopy(endDate);
    payload["cacheSnapshot"] = std::move(cacheSnapshot);

    return payload.dump();
}

std::string md5Hex(const std::string& input)
{
    std::vector<std::uint8_t> message(input.begin(), input.end());
    const std::uint64_t bitLength = static_cast<std::uint64_t>(message.size()) * 8ULL;

    message.push_back(0x80U);
    while ((message.size() % 64U) != 56U) {
        message.push_back(0x00U);
    }

    for (int i = 0; i < 8; ++i) {
        message.push_back(static_cast<std::uint8_t>((bitLength >> (8 * i)) & 0xffU));
    }

    std::uint32_t a0 = 0x67452301U;
    std::uint32_t b0 = 0xefcdab89U;
    std::uint32_t c0 = 0x98badcfeU;
    std::uint32_t d0 = 0x10325476U;

    for (size_t offset = 0; offset < message.size(); offset += 64U) {
        std::uint32_t m[16]{};
        for (int i = 0; i < 16; ++i) {
            const size_t index = offset + static_cast<size_t>(i) * 4U;
            m[i] = static_cast<std::uint32_t>(message[index])
                | (static_cast<std::uint32_t>(message[index + 1]) << 8U)
                | (static_cast<std::uint32_t>(message[index + 2]) << 16U)
                | (static_cast<std::uint32_t>(message[index + 3]) << 24U);
        }

        std::uint32_t a = a0;
        std::uint32_t b = b0;
        std::uint32_t c = c0;
        std::uint32_t d = d0;

        for (int i = 0; i < 64; ++i) {
            std::uint32_t f = 0U;
            std::uint32_t g = 0U;
            if (i < 16) {
                f = (b & c) | ((~b) & d);
                g = static_cast<std::uint32_t>(i);
            } else if (i < 32) {
                f = (d & b) | ((~d) & c);
                g = static_cast<std::uint32_t>((5 * i + 1) % 16);
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = static_cast<std::uint32_t>((3 * i + 5) % 16);
            } else {
                f = c ^ (b | (~d));
                g = static_cast<std::uint32_t>((7 * i) % 16);
            }

            const std::uint32_t temp = d;
            d = c;
            c = b;
            b = b + leftRotate(a + f + kMd5K[static_cast<size_t>(i)] + m[g], kMd5Shift[static_cast<size_t>(i)]);
            a = temp;
        }

        a0 += a;
        b0 += b;
        c0 += c;
        d0 += d;
    }

    std::array<std::uint8_t, 16> digest{};
    const std::uint32_t words[4] = {a0, b0, c0, d0};
    for (int i = 0; i < 4; ++i) {
        digest[static_cast<size_t>(i) * 4U] = static_cast<std::uint8_t>(words[i] & 0xffU);
        digest[static_cast<size_t>(i) * 4U + 1U] = static_cast<std::uint8_t>((words[i] >> 8U) & 0xffU);
        digest[static_cast<size_t>(i) * 4U + 2U] = static_cast<std::uint8_t>((words[i] >> 16U) & 0xffU);
        digest[static_cast<size_t>(i) * 4U + 3U] = static_cast<std::uint8_t>((words[i] >> 24U) & 0xffU);
    }

    return bytesToHex(digest);
}

std::string buildScopeKeyHexMd5(const std::string& dataSourceMode,
                                int selectedDatasetId,
                                const std::string& startDate,
                                const std::string& endDate,
                                const std::string& cacheSnapshotJson)
{
    return md5Hex(buildScopePayloadJson(
        dataSourceMode,
        selectedDatasetId,
        startDate,
        endDate,
        cacheSnapshotJson));
}

} // namespace factor::check
