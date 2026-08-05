// DataCache.h — 纯 C++ 数据集缓存（零 Qt）
// 替代 DataServiceCache 的核心功能，与 bin 缓存协作
#pragma once
#include "DataFieldKeys.h"
#include "foundation/json/json_facade.h"
#include "foundation/log/logging.hpp"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <ankerl/unordered_dense.h>

#ifdef ASTOCK_HAS_PARQUET
namespace arrow { class Table; }
#endif

namespace cleaning {

using JCache = foundation::json::JsonFacade;

// ── 数据集元数据（纯 C++ 版本） ──
struct DataSetInfo {
    int id{-1};
    std::string displayName;
    std::string description;
    std::string sourceType;       // "cleaning", "query", "import"
    int64_t createdAt{0};         // unix timestamp
    int rowCount{0};
    int schemaVersion{2};
    int sourceDataSetId{-1};       // 来源数据集ID（清洗结果指向原始raw数据集，拉取结果指向配置ID）
    std::vector<std::string> availableFields;
    std::vector<std::string> stockCodes;
    std::string startDate;        // "YYYY-MM-DD"
    std::string endDate;
    std::vector<std::string> tags;
    bool isBacktestReady{false};

    JCache toJson() const {
        auto obj = JCache::createObject();
        obj.set("id", JCache::createDouble(static_cast<double>(id)));
        obj.set("displayName", JCache::createString(displayName));
        obj.set("description", JCache::createString(description));
        obj.set("sourceType", JCache::createString(sourceType));
        obj.set("createdAt", JCache::createDouble(static_cast<double>(createdAt)));
        obj.set("rowCount", JCache::createDouble(static_cast<double>(rowCount)));
        obj.set("schemaVersion", JCache::createDouble(static_cast<double>(schemaVersion)));
        if (sourceDataSetId > 0) obj.set("sourceDataSetId", JCache::createDouble(static_cast<double>(sourceDataSetId)));
        auto fieldsArr = JCache::createArray();
        for (const auto& f : availableFields) fieldsArr.push_back(JCache::createString(f));
        obj.set("availableFields", std::move(fieldsArr));
        auto scArr = JCache::createArray();
        for (const auto& s : stockCodes) scArr.push_back(JCache::createString(s));
        obj.set("stockCodes", std::move(scArr));
        obj.set("startDate", JCache::createString(startDate));
        obj.set("endDate", JCache::createString(endDate));
        if (!tags.empty()) {
            auto tArr = JCache::createArray();
            for (const auto& t : tags) tArr.push_back(JCache::createString(t));
            obj.set("tags", std::move(tArr));
        }
        obj.set("isBacktestReady", JCache::createBool(isBacktestReady));
        return obj;
    }

    static DataSetInfo fromJson(const JCache& obj) {
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
        if (obj.has("sourceDataSetId")) info.sourceDataSetId = static_cast<int>(obj.get("sourceDataSetId").asDouble());
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
        INTERNAL_INFO_STREAM << "[DataCache] initialized, dir=" << m_persistentDir << ", nextId=" << m_nextDataSetId << ", existing datasets=" << m_index.size();
        return true;
    }

    bool isInitialized() const { return m_initialized; }

    // ── 存储数据集 ──
    int storeDataSet(const std::vector<JCache>& data, const DataSetInfo& info,
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

        INTERNAL_INFO_STREAM << "[DataCache] stored dataset " << dataId << ": " << fullInfo.displayName << " (" << fullInfo.rowCount << " rows, " << fullInfo.availableFields.size() << " fields)";
        return dataId;
    }

    // ── 更新数据集行数 ──
    void updateDataSetRowCount(int dataId, int rowCount) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_index.find(dataId);
        if (it != m_index.end()) { it->second.rowCount = rowCount; saveCatalog(); }
    }

    // ── 获取数据集信息 ──
    DataSetInfo getDataSetInfo(int dataId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_index.find(dataId);
        return it != m_index.end() ? it->second : DataSetInfo{};
    }

    // ── 删除数据集 ──
    /// @brief 删除数据集（递归删除整个 dataset_X/ 目录树 + 清理索引）
    bool removeDataSet(int dataId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_index.find(dataId);
        if (it == m_index.end()) return false;
        std::string dir = datasetDir(dataId);
        m_index.erase(it);
        saveCatalog();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        if (ec) {
            INTERNAL_ERROR_STREAM << "[DataCache] removeDataSet " << dataId
                << " dir cleanup failed: " << ec.message() << " (index already removed)";
            return false;
        }
        INTERNAL_INFO_STREAM << "[DataCache] removed dataset " << dataId << " dir=" << dir;
        return true;
    }

    /// @brief 按 sourceType 批量删除数据集（递归删除整个目录树）
    int removeDataSetsBySourceType(const std::string& sourceType) {
        std::lock_guard<std::mutex> lock(m_mutex);
        int removed = 0; std::vector<int> ids;
        for (const auto& [id, info] : m_index) if (info.sourceType == sourceType) ids.push_back(id);
        for (int id : ids) {
            std::error_code ec;
            std::filesystem::remove_all(datasetDir(id), ec);
            m_index.erase(id);
            ++removed;
            if (ec) {
                INTERNAL_ERROR_STREAM << "[DataCache] removeDataSetsBySourceType: "
                    << "dir cleanup failed for " << id << ": " << ec.message();
            }
        }
        if (removed > 0) saveCatalog();
        INTERNAL_INFO_STREAM << "[DataCache] removed " << removed << " datasets by sourceType=" << sourceType;
        return removed;
    }

    // ── 列出所有数据集 ──
    std::vector<DataSetInfo> listDataSets() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<DataSetInfo> result;
        for (const auto& [id, info] : m_index) result.push_back(info);
        return result;
    }

    // ── 数据文件持久化（Parquet 列式存储） ──

    // 批次写入会话（由 beginArrowWrite 创建）
    struct WriteSession { int dataId = -1; int64_t totalRows = 0; };
    typedef WriteSession* ArrowWriteToken;

#ifdef ASTOCK_HAS_PARQUET

    /// @brief 保存数据集数据到 Arrow 文件（自动扫描字段）
    void saveDataSetFile(int dataId, const std::vector<JCache>& rows);
    /// @brief 保存数据集数据到 Arrow 文件（显式字段，不扫描）
    void saveDataSetFile(int dataId, const std::vector<JCache>& rows,
        const std::vector<std::string>& fieldNames,
        const std::unordered_set<std::string>& numericFields);

    /// @brief 从 Arrow 文件加载数据集数据
    std::vector<JCache> loadDataSetFile(int dataId);

    /// @brief 加载缓存中 trade_date >= sinceDate 的所有行（完整列），
    /// 用于增量清洗构建回溯窗口上下文。返回按文件原有顺序排列的完整 LightRow/JsonFacade 向量。
    /// @param dataId 数据集 ID
    /// @param sinceDate 起始日期（含），格式 "YYYY-MM-DD"
    std::vector<JCache> loadDataSetRange(int dataId, const std::string& sinceDate);

    /// @brief 加载指定 symbol 的所有行（完整列，按文件原有顺序），用于缓存数据查看/清洗前后对比
    std::vector<JCache> loadDataSetRowsBySymbol(int dataId, const std::string& symbol);

    /// @brief 增量追加：读取现有 .arrow 全部 RecordBatch，与新数据合并后写入
    /// 临时文件，最后原子替换（std::filesystem::rename）。
    /// 保证：要么原子换成含新数据的文件，要么旧文件完好如初，不存在中间态。
    /// @return 追加后的总行数；失败返回 -1（旧文件不变）
    int appendDataSetFile(int dataId, const std::vector<JCache>& newRows,
        const std::vector<std::string>& fieldNames,
        const std::unordered_set<std::string>& numericFields);

    /// @brief 扫描 .arrow 文件的 trade_date 列，返回真实最大 trade_date（"YYYY-MM-DD"）。
    /// 以文件内容为准，不依赖 DataSetInfo 元数据；文件不存在/无该列返回空串。
    std::string getMaxTradeDate(int dataId);

    /// @brief 从 Arrow 文件加载为 Arrow Table
    std::shared_ptr<arrow::Table> loadDataSetTable(int dataId);

    /// @brief 仅读取 Arrow 文件 schema 字段名（零数据行加载，O(1) 内存）
    std::vector<std::string> loadDataSetSchemaFields(int dataId);

    /// @brief 批量写入：开始新 Arrow 文件（自动从数据扫描字段类型）
    ArrowWriteToken beginArrowWrite(int dataId);
    /// @brief 批量写入：指定字段列表和类型，避免硬编码扫描
    ArrowWriteToken beginArrowWrite(int dataId,
        const std::vector<std::string>& fieldNames,
        const std::unordered_set<std::string>& numericFields);

    /// @brief 批量写入：追加一批行（token 由 beginArrowWrite 返回）
    void appendArrowBatch(ArrowWriteToken token, const std::vector<JCache>& rows);

    /// @brief 批量写入：直接追加 Arrow Table（跳过 JsonFacade）
    void appendArrowTable(ArrowWriteToken token, const std::shared_ptr<arrow::Table>& table);

    /// @brief 批量写入：完成并关闭文件
    void finishArrowWrite(ArrowWriteToken token);

#endif // ASTOCK_HAS_PARQUET

    /// @brief 保存数据集数据到 JSON 文件（无 Parquet 时的回退）
    void saveDataSetFileJson(int dataId, const std::vector<JCache>& rows) {
        auto arr = JCache::createArray();
        for (const auto& row : rows) arr.push_back(row);
        writeFile(jsonDataFilePath(dataId), arr.toString());
    }

    /// @brief 从 JSON 文件加载数据集数据（无 Parquet 时的回退）
    std::vector<JCache> loadDataSetFileJson(int dataId) {
        std::string content = readFile(jsonDataFilePath(dataId));
        if (content.empty()) return {};
        auto root = JCache::parse(content);
        if (!root.isArray()) return {};
        std::vector<JCache> rows;
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
    std::string datasetDir(int dataId) const {
        return m_persistentDir + "/dataset_" + std::to_string(dataId);
    }
    std::string binFilePath(int dataId) const {
        return datasetDir(dataId) + "/raw/data.bin";
    }
    std::string dataFilePath(int dataId) const {
        return datasetDir(dataId) + "/raw/data.arrow";
    }
    std::string jsonDataFilePath(int dataId) const {
        return datasetDir(dataId) + "/raw/data.json";
    }
    std::string cleanedFilePath(int dataId) const {
        return datasetDir(dataId) + "/cleaned/data.arrow";
    }
    std::string infoFilePath(int dataId) const {
        return datasetDir(dataId) + "/info.json";
    }

private:
    DataCache() = default;

    std::string catalogPath() const {
        return m_persistentDir + "/dataset_catalog.json";
    }

    void saveCatalog() {
        auto obj = JCache::createObject();
        obj.set("nextId", JCache::createDouble(static_cast<double>(m_nextDataSetId)));
        auto arr = JCache::createArray();
        for (const auto& [id, info] : m_index) arr.push_back(info.toJson());
        obj.set("datasets", std::move(arr));
        writeFile(catalogPath(), obj.toString());
    }

    void loadCatalog() {
        std::string content = readFile(catalogPath());
        bool fromCatalog = false;
        if (!content.empty()) {
            auto root = JCache::parse(content);
            if (root.isObject()) {
                if (root.has("nextId")) m_nextDataSetId = static_cast<int>(root.get("nextId").asDouble());
                if (root.has("datasets") && root.get("datasets").isArray()) {
                    auto arr = root.get("datasets");
                    for (size_t i = 0; i < arr.size(); ++i) {
                        auto info = DataSetInfo::fromJson(arr.at(i));
                        // 跳过数据文件已被删除的无效条目
                        std::string apath = dataFilePath(info.id);
                        FILE* ftest = fopen(apath.c_str(), "rb");
                        if (!ftest) {
                            INTERNAL_WARN_STREAM << "[DataCache] skip stale dataset " << info.id << " (no file)";
                            std::error_code ec;
                            std::filesystem::remove_all(datasetDir(info.id), ec);
                            m_indexStale = true;
                            continue;
                        }
                        fclose(ftest);
                        m_index[info.id] = info;
                    }
                    fromCatalog = true;
                }
            }
        }
        // 剔除脏文件后重写 catalog
        if (m_indexStale) { saveCatalog(); m_indexStale = false; }
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
            auto infoJson = JCache::parse(content);
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
        // 确保父目录存在
        auto pos = path.rfind('/');
        if (pos == std::string::npos) pos = path.rfind('\\');
        if (pos != std::string::npos) ensureDir(path.substr(0, pos));
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) {
            INTERNAL_ERROR_STREAM << "[DataCache] writeFile: fopen failed for " << path;
            return;
        }
        size_t written = fwrite(content.data(), 1, content.size(), f);
        if (written != content.size()) {
            INTERNAL_ERROR_STREAM << "[DataCache] writeFile: fwrite partial write (" << written << " / " << content.size() << " bytes) for " << path;
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
            INTERNAL_ERROR_STREAM << "[DataCache] readFile: fread partial read (" << nread << " / " << sz << " bytes) for " << path;
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
    bool m_indexStale{false};
};

} // namespace cleaning
