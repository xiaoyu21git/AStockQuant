// DataCache.h — 纯 C++ 数据集缓存（零 Qt）
// 替代 DataServiceCache 的核心功能，与 bin 缓存协作
#pragma once
#include "DataFieldKeys.h"
#include "foundation/json/json_facade.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <ankerl/unordered_dense.h>

namespace cleaning {

using J = foundation::json::JsonFacade;

// ── 数据集元数据（纯 C++ 版本） ──
struct DataSetInfo {
    int id{-1};
    std::string displayName;
    std::string description;
    std::string sourceType;       // "cleaning", "query", "import"
    int64_t createdAt{0};         // unix timestamp
    int rowCount{0};
    int schemaVersion{2};
    std::vector<std::string> availableFields;
    std::vector<std::string> stockCodes;
    std::string startDate;        // "YYYY-MM-DD"
    std::string endDate;
    std::vector<std::string> tags;
    bool isBacktestReady{false};

    J toJson() const {
        auto obj = J::createObject();
        obj.set("id", J::createDouble(static_cast<double>(id)));
        obj.set("displayName", J::createString(displayName));
        obj.set("description", J::createString(description));
        obj.set("sourceType", J::createString(sourceType));
        obj.set("createdAt", J::createDouble(static_cast<double>(createdAt)));
        obj.set("rowCount", J::createDouble(static_cast<double>(rowCount)));
        obj.set("schemaVersion", J::createDouble(static_cast<double>(schemaVersion)));
        auto fieldsArr = J::createArray();
        for (const auto& f : availableFields) fieldsArr.push_back(J::createString(f));
        obj.set("availableFields", std::move(fieldsArr));
        auto scArr = J::createArray();
        for (const auto& s : stockCodes) scArr.push_back(J::createString(s));
        obj.set("stockCodes", std::move(scArr));
        obj.set("startDate", J::createString(startDate));
        obj.set("endDate", J::createString(endDate));
        if (!tags.empty()) {
            auto tArr = J::createArray();
            for (const auto& t : tags) tArr.push_back(J::createString(t));
            obj.set("tags", std::move(tArr));
        }
        obj.set("isBacktestReady", J::createBool(isBacktestReady));
        return obj;
    }

    static DataSetInfo fromJson(const J& obj) {
        DataSetInfo info;
        if (obj.has("id")) info.id = static_cast<int>(obj.get("id").asDouble());
        if (obj.has("displayName")) info.displayName = obj.get("displayName").asString();
        if (obj.has("description")) info.description = obj.get("description").asString();
        if (obj.has("sourceType")) info.sourceType = obj.get("sourceType").asString();
        if (obj.has("createdAt")) info.createdAt = static_cast<int64_t>(obj.get("createdAt").asDouble());
        else if (obj.has("createdTime")) {
            // 兼容旧 DataServiceCache 格式（QDateTime 字符串）
            std::string ts = obj.get("createdTime").asString();
            if (!ts.empty()) info.createdAt = static_cast<int64_t>(std::stoll(ts));
        }
        if (obj.has("rowCount")) info.rowCount = static_cast<int>(obj.get("rowCount").asDouble());
        if (obj.has("schemaVersion")) info.schemaVersion = static_cast<int>(obj.get("schemaVersion").asDouble());
        if (obj.has("availableFields") && obj.get("availableFields").isArray()) {
            auto arr = obj.get("availableFields");
            for (size_t i = 0; i < arr.size(); ++i) info.availableFields.push_back(arr.at(i).asString());
        }
        if (obj.has("stockCodes") && obj.get("stockCodes").isArray()) {
            auto arr = obj.get("stockCodes");
            for (size_t i = 0; i < arr.size(); ++i) info.stockCodes.push_back(arr.at(i).asString());
        }
        if (obj.has("tags") && obj.get("tags").isArray()) {
            auto arr = obj.get("tags");
            for (size_t i = 0; i < arr.size(); ++i) info.tags.push_back(arr.at(i).asString());
        }
        if (obj.has("startDate")) info.startDate = obj.get("startDate").asString();
        if (obj.has("endDate")) info.endDate = obj.get("endDate").asString();
        if (obj.has("isBacktestReady")) info.isBacktestReady = obj.get("isBacktestReady").asBool();
        return info;
    }
};

// ── 纯 C++ 数据集缓存 ──
class DataCache {
public:
    using ProgressCallback = std::function<void(int current, int total)>;

    static DataCache& instance() {
        static DataCache s_instance;
        return s_instance;
    }

    /// @brief 初始化（设置持久化目录）
    bool initialize(const std::string& persistentDir) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_initialized) return true;
        m_persistentDir = persistentDir;
        // 确保目录存在
        ensureDir(m_persistentDir);
        // 加载已有索引
        loadCatalog();
        m_initialized = true;
        fprintf(stderr, "[DataCache] initialized, dir=%s, nextId=%d, existing datasets=%zu\n",
                m_persistentDir.c_str(), m_nextDataSetId, m_index.size());
        fflush(stderr);
        return true;
    }

    bool isInitialized() const { return m_initialized; }

    // ── 存储数据集 ──
    int storeDataSet(const std::vector<J>& data, const DataSetInfo& info,
                     ProgressCallback onProgress = {}) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_initialized) return -1;

        // 用 max(nextId, max_existing + 1) 防止空洞，删除后 ID 可复用
        int maxExisting = 0;
        for (const auto& [id, _] : m_index)
            if (id > maxExisting) maxExisting = id;
        m_nextDataSetId = (std::max)(m_nextDataSetId, maxExisting + 1);
        int dataId = m_nextDataSetId++;
        DataSetInfo fullInfo = info;
        fullInfo.id = dataId;
        fullInfo.rowCount = static_cast<int>(data.size());
        if (fullInfo.createdAt == 0) fullInfo.createdAt = currentTimestamp();

        // 扫描可用字段
        if (fullInfo.availableFields.empty()) {
            std::unordered_set<std::string> fields;
            for (const auto& row : data) {
                if (row.isObject()) {
                    // 简单遍历（JsonFacade 的 keys 需要迭代）
                    for (const char* key : {CF::SYMBOL.c_str(), CF::TRADE_DATE.c_str(),
                                            MF::OPEN.c_str(), MF::HIGH.c_str(), MF::LOW.c_str(),
                                            MF::CLOSE.c_str(), MF::VOLUME.c_str(), MF::PRE_ADJ_FACTOR.c_str(),
                                            MF::POST_ADJ_FACTOR.c_str(), MF::MARKET_CAP.c_str(),
                                            MF::PE_RATIO.c_str(), MF::PB_RATIO.c_str(),
                                            MF::TURNOVER_RATE.c_str(), MF::INDUSTRY_CODE.c_str()}) {
                        if (row.has(key)) fields.insert(key);
                    }
                }
            }
            fullInfo.availableFields.assign(fields.begin(), fields.end());
        }

        // 持久化元数据
        auto metaJson = fullInfo.toJson();
        writeFile(infoFilePath(dataId), metaJson.toString());

        // 更新索引
        m_index[dataId] = fullInfo;
        saveCatalog();

        fprintf(stderr, "[DataCache] stored dataset %d: %s (%d rows, %zu fields)\n",
                dataId, fullInfo.displayName.c_str(), fullInfo.rowCount, fullInfo.availableFields.size());
        fflush(stderr);
        return dataId;
    }

    // ── 获取数据集信息 ──
    DataSetInfo getDataSetInfo(int dataId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_index.find(dataId);
        return it != m_index.end() ? it->second : DataSetInfo{};
    }

    // ── 删除数据集 ──
    bool removeDataSet(int dataId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_index.find(dataId);
        if (it == m_index.end()) return false;
        std::remove(infoFilePath(dataId).c_str());
        std::remove(binFilePath(dataId).c_str());
        std::remove(dataFilePath(dataId).c_str());
        std::remove(jsonDataFilePath(dataId).c_str());
        m_index.erase(it);
        saveCatalog();
        return true;
    }

    // ── 列出所有数据集 ──
    std::vector<DataSetInfo> listDataSets() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<DataSetInfo> result;
        for (const auto& [id, info] : m_index) result.push_back(info);
        return result;
    }

    // ── 数据文件持久化（Parquet 列式存储） ──

#ifdef ASTOCK_HAS_PARQUET

    /// @brief 保存数据集数据到 Arrow 文件（一次性）
    void saveDataSetFile(int dataId, const std::vector<J>& rows);

    /// @brief 从 Arrow 文件加载数据集数据
    std::vector<J> loadDataSetFile(int dataId);

    /// @brief 批量写入：开始新 Arrow 文件（返回 writer token，null 表示失败）
    void* beginArrowWrite(int dataId);

    /// @brief 批量写入：追加一批行（token 由 beginArrowWrite 返回）
    void appendArrowBatch(void* token, const std::vector<J>& rows);

    /// @brief 批量写入：完成并关闭文件
    void finishArrowWrite(void* token);

#endif // ASTOCK_HAS_PARQUET

    /// @brief 保存数据集数据到 JSON 文件（无 Parquet 时的回退）
    void saveDataSetFileJson(int dataId, const std::vector<J>& rows) {
        auto arr = J::createArray();
        for (const auto& row : rows) arr.push_back(row);
        writeFile(jsonDataFilePath(dataId), arr.toString());
    }

    /// @brief 从 JSON 文件加载数据集数据（无 Parquet 时的回退）
    std::vector<J> loadDataSetFileJson(int dataId) {
        std::string content = readFile(jsonDataFilePath(dataId));
        if (content.empty()) return {};
        auto root = J::parse(content);
        if (!root.isArray()) return {};
        std::vector<J> rows;
        rows.reserve(root.size());
        for (size_t i = 0; i < root.size(); ++i) rows.push_back(root.at(i));
        return rows;
    }

    /// @brief 按条件过滤数据集
    std::vector<DataSetInfo> queryDataSets(const std::string& sourceTypeFilter = {},
                                           const std::string& stockCodeFilter = {},
                                           const std::string& dateFilter = {}) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<DataSetInfo> result;
        for (const auto& [_, info] : m_index) {
            if (!sourceTypeFilter.empty() && info.sourceType != sourceTypeFilter) continue;
            if (!dateFilter.empty() && !(info.startDate <= dateFilter && info.endDate >= dateFilter)) continue;
            result.push_back(info);
        }
        return result;
    }

    // ── 文件路径（供外部直接读写） ──
    std::string binFilePath(int dataId) const {
        return m_persistentDir + "/dataset_" + std::to_string(dataId) + "_data.bin";
    }
    std::string dataFilePath(int dataId) const {
        return m_persistentDir + "/dataset_" + std::to_string(dataId) + "_data.arrow";
    }
    std::string jsonDataFilePath(int dataId) const {
        return m_persistentDir + "/dataset_" + std::to_string(dataId) + "_data.json";
    }
    std::string infoFilePath(int dataId) const {
        return m_persistentDir + "/dataset_" + std::to_string(dataId) + "_info.json";
    }

private:
    DataCache() = default;

    std::string catalogPath() const {
        return m_persistentDir + "/dataset_catalog.json";
    }

    void saveCatalog() {
        auto obj = J::createObject();
        obj.set("nextId", J::createDouble(static_cast<double>(m_nextDataSetId)));
        auto arr = J::createArray();
        for (const auto& [id, info] : m_index) arr.push_back(info.toJson());
        obj.set("datasets", std::move(arr));
        writeFile(catalogPath(), obj.toString());
    }

    void loadCatalog() {
        std::string content = readFile(catalogPath());
        bool fromCatalog = false;
        if (!content.empty()) {
            auto root = J::parse(content);
            if (root.isObject()) {
                if (root.has("nextId")) m_nextDataSetId = static_cast<int>(root.get("nextId").asDouble());
                if (root.has("datasets") && root.get("datasets").isArray()) {
                    auto arr = root.get("datasets");
                    for (size_t i = 0; i < arr.size(); ++i) {
                        auto info = DataSetInfo::fromJson(arr.at(i));
                        m_index[info.id] = info;
                    }
                    fromCatalog = true;
                }
            }
        }
        // 扫描独立 info 文件（兼容旧 DataServiceCache 格式）
        if (!fromCatalog) {
            scanIndividualInfoFiles();
        }
    }

    // ── 扫描 dataset_*_info.json 文件，导入到统一 catalog ──
    void scanIndividualInfoFiles() {
        // 尝试加载 dataset_1_info.json 到 dataset_1000_info.json
        // 老 DataServiceCache 每个数据集存为独立 info 文件
        for (int id = 1; id <= 1000; ++id) {
            if (m_index.find(id) != m_index.end()) continue; // 已在 catalog 中
            std::string content = readFile(infoFilePath(id));
            if (content.empty()) continue;
            auto infoJson = J::parse(content);
            if (!infoJson.isObject()) continue;
            auto info = DataSetInfo::fromJson(infoJson);
            info.id = id;
            m_index[id] = info;
            if (id >= m_nextDataSetId) m_nextDataSetId = id + 1;
        }
    }

    // ── 文件 I/O（纯 C++） ──
    static void ensureDir(const std::string& path) {
        // 创建目录（跨平台）
#ifdef _MSC_VER
        std::string cmd = "mkdir \"" + path + "\" 2>nul";
#else
        std::string cmd = "mkdir -p \"" + path + "\"";
#endif
        std::system(cmd.c_str());
    }

    static void writeFile(const std::string& path, const std::string& content) {
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) {
            fprintf(stderr, "[DataCache] writeFile: fopen failed for %s\n", path.c_str());
            fflush(stderr);
            return;
        }
        size_t written = fwrite(content.data(), 1, content.size(), f);
        if (written != content.size()) {
            fprintf(stderr, "[DataCache] writeFile: fwrite partial write (%zu / %zu bytes) for %s\n",
                    written, content.size(), path.c_str());
            fflush(stderr);
            fclose(f);
            // 删除部分写入的损坏文件
            std::remove(path.c_str());
            return;
        }
        fclose(f);
    }

    static std::string readFile(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return {};
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0) { fclose(f); return {}; }
        std::string content(static_cast<size_t>(sz), '\0');
        size_t nread = fread(&content[0], 1, static_cast<size_t>(sz), f);
        fclose(f);
        if (nread != static_cast<size_t>(sz)) {
            fprintf(stderr, "[DataCache] readFile: fread partial read (%zu / %ld bytes) for %s\n",
                    nread, sz, path.c_str());
            fflush(stderr);
            return {};
        }
        return content;
    }

    static int64_t currentTimestamp() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    mutable std::mutex m_mutex;
    bool m_initialized{false};
    std::string m_persistentDir;
    int m_nextDataSetId{1};
    ankerl::unordered_dense::map<int, DataSetInfo> m_index;
};

} // namespace cleaning
