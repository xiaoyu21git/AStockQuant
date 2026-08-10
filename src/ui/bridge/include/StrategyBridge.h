#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "StrategyLifecycleStatus.h"
#include "../../domain/types/ResolvedStrategyBehavior.h"
#include "foundation/thread/ThreadPoolExecutor.h"
#include "../../domain/strategies/include/StrategyDefinitionTypes.h"

#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace factor::compute { class IMarketDataView; class CachedMarketDataView; }

namespace domain::strategy {
class StrategyEngine;
}

class StrategyListModel;

#include "database/StrategyRepository.h"
#include "../../domain/strategy/include/IBasketInterceptor.h"

class StrategyBridge : public QObject, public domain::strategy::IBasketInterceptor {
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errMsg READ errMsg NOTIFY errMsgChanged)
    Q_PROPERTY(bool inited READ inited NOTIFY initedChanged)
    Q_PROPERTY(bool cacheOk READ cacheOk NOTIFY cacheOkChanged)
    Q_PROPERTY(QString selId READ selId WRITE setSelId NOTIFY selIdChanged)
    Q_PROPERTY(QAbstractListModel* listModel READ listModel CONSTANT)
    Q_PROPERTY(QVariantList pendingBasketOrders READ pendingBasketOrders NOTIFY pendingBasketChanged)
    Q_PROPERTY(QString pendingBasketStrategyName READ pendingBasketStrategyName NOTIFY pendingBasketChanged)
    Q_PROPERTY(QString pendingBasketContextDesc READ pendingBasketContextDesc NOTIFY pendingBasketChanged)
    Q_PROPERTY(bool hasPendingBasket READ hasPendingBasket NOTIFY pendingBasketChanged)

public:
    explicit StrategyBridge(QObject* parent = nullptr);
    ~StrategyBridge() override;

    Q_INVOKABLE void init();
    Q_INVOKABLE void initAsync();
    Q_INVOKABLE bool inited() const;
    Q_INVOKABLE bool cacheOk() const;

    Q_INVOKABLE QString add(const QVariantMap& payload);
    Q_INVOKABLE bool update(const QVariantMap& payload);
    Q_INVOKABLE bool remove(const QString& strategyId);
    Q_INVOKABLE QVariantMap get(const QString& strategyId);
    Q_INVOKABLE QVariantList list();
    Q_INVOKABLE bool start(const QString& strategyId);
    Q_INVOKABLE bool stop(const QString& strategyId);
    Q_INVOKABLE int liquidateAll(const QString& strategyId);
    Q_INVOKABLE bool saveViewCfg(const QString& strategyId, const QVariantMap& visualConfig);

    /// @brief 为实盘策略设置行情视图 (QML 可调用)
    /// @param datasetJson 包含足够回溯窗口的 OHLCV JSON 数组字符串
    Q_INVOKABLE void setupLiveMarketView(const QString& strategyId, const QString& datasetJson);

    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString errMsg() const;
    [[nodiscard]] QString selId() const;
    [[nodiscard]] QAbstractListModel* listModel() const;
    void setSelId(const QString& strategyId);

    [[nodiscard]] Q_INVOKABLE domain::strategy::StrategyEngine* backtestEngineProvider(const QString& strategyId);

    /// @brief 单行刷新 — 仅重新查询并更新指定策略在 listModel 中的行，不触发全量 replaceAll
    Q_INVOKABLE void refreshSingleStrategy(const QString& strategyId);

    /// @brief symbol → "股票中文名 代码" (如 "平安银行 000001.SZ")
    Q_INVOKABLE QString stockDisplayName(const QString& symbol) const;

    // ── 半自动确认窗口 (v0.16.0) ──

    /// @brief 用户确认篮子订单 → 提交至 TradeExecutionEngine
    /// QML BasketConfirmDialog 调用, editedOrders 中的 quantity 可能被用户修改
    Q_INVOKABLE void confirmBasket(const QVariantList& editedOrders);

    /// @brief 用户拒绝篮子 → 丢弃全部订单
    Q_INVOKABLE void rejectBasket();

    /// @brief [测试] 发射合成测试篮子 (3条样本订单), 用于验证 SemiAuto 确认窗口链路
    Q_INVOKABLE void testEmitBasket(const QString& strategyId);

    /// @brief 当前待确认订单列表 (QML 显示用)
    [[nodiscard]] QVariantList pendingBasketOrders() const;
    /// @brief 当前待确认篮子的策略名称
    [[nodiscard]] QString pendingBasketStrategyName() const;
    /// @brief 当前待确认篮子的上下文描述
    [[nodiscard]] QString pendingBasketContextDesc() const;
    /// @brief 是否有待确认的篮子
    [[nodiscard]] bool hasPendingBasket() const;

    // ── 策略类型枚举 (替代 JS StrategyCreationUtils 的数字映射) ──
    /// @brief 策略类型索引 → 中文名
    Q_INVOKABLE QString strategyTypeName(int typeIndex) const;
    /// @brief 策略类型索引 → 图标
    Q_INVOKABLE QString strategyTypeIcon(int typeIndex) const;
    /// @brief 策略类型索引 → 简短描述
    Q_INVOKABLE QString strategyTypeBrief(int typeIndex) const;
    /// @brief 策略类型索引 → behaviorKind
    Q_INVOKABLE int strategyBehaviorKindFromTypeIndex(int typeIndex) const;
    /// @brief behaviorKind → 策略类型索引
    Q_INVOKABLE int strategyTypeIndexFromBehaviorKind(int behaviorKind) const;
    /// @brief 标准化策略类型索引 (接受 display/behavior 两种编号)
    Q_INVOKABLE int normalizeStrategyTypeIndex(int raw) const;
    /// @brief 风险等级索引 → 中文名
    Q_INVOKABLE QString riskLevelName(int index) const;
    /// @brief 风险等级索引 → 颜色
    Q_INVOKABLE QString riskLevelColor(int index) const;

    /// @brief 策略参数配置表单 (替代 JS buildParamConfigs)
    Q_INVOKABLE QVariantList buildParamConfigs(int typeIndex) const;
    /// @brief 组装完整策略创建数据 (替代 JS buildCompleteStrategyData)
    Q_INVOKABLE QVariantMap buildCompleteStrategyData(const QVariantMap& context) const;
    /// @brief 重置表单数据默认值 (替代 JS resetFormData)
    Q_INVOKABLE QVariantMap resetFormData() const;
    /// @brief 默认策略描述 (替代 JS getDefaultStrategyDescription)
    Q_INVOKABLE QString defaultStrategyDescription(int typeIndex) const;
    /// @brief 默认策略标签 (替代 JS getDefaultStrategyTags)
    Q_INVOKABLE QStringList defaultStrategyTags(int typeIndex) const;

    // ── 规则编辑器 (替代 JS rule composer) ──
    Q_INVOKABLE QVariantMap buildDefaultStrategyProfile(int typeIndex) const;
    Q_INVOKABLE QVariantList buildDefaultBaseRuleBindings(const QVariantMap& profile) const;
    Q_INVOKABLE QVariantList buildDefaultMarketRuleBindings(const QVariantMap& profile) const;
    Q_INVOKABLE QVariantList buildDefaultRuleComposerSkeleton(const QVariantMap& profile, const QVariantList& bindings) const;
    Q_INVOKABLE QVariantMap validateRuleComposerConfiguration(const QVariantMap& profile, const QVariantList& stages) const;
    Q_INVOKABLE QString resolveRuleTemplateFileName(const QString& templateId) const;

    // ── 模板洞察 (替代 JS template insight) ──
    Q_INVOKABLE QString getTemplateInsight(const QVariantMap& rule) const;
    Q_INVOKABLE QString insightSectionTitle(const QString& phaseKey) const;
    Q_INVOKABLE QString insightPrimaryTitle(const QString& phaseKey) const;
    Q_INVOKABLE QVariantList insightPrimaryItems(const QVariantMap& rule) const;
    Q_INVOKABLE QString insightSecondaryTitle(const QString& phaseKey) const;
    Q_INVOKABLE QVariantList insightSecondaryItems(const QVariantMap& rule) const;
    Q_INVOKABLE QString normalizePhaseKey(const QString& raw) const;
    Q_INVOKABLE QString phaseDisplayName(const QString& phaseKey) const;
    Q_INVOKABLE QString phaseShortName(const QString& phaseKey) const;
    Q_INVOKABLE QString categoryDisplayName(const QString& category) const;
    Q_INVOKABLE QString actionDisplayName(const QString& action) const;
    Q_INVOKABLE QVariantMap templateInsight(const QString& templateId) const;

    // ── 翻译 (替代 JS tr) ──
    Q_INVOKABLE QString tr(const QString& key, const QString& language = QString()) const;

    /// @brief C++ 内部获取单例，供 backtest bridge 等内部组件使用
    static StrategyBridge* instance();

signals:
    void busyChanged();
    void errMsgChanged();
    void initedChanged();
    void cacheOkChanged();
    void selIdChanged();

    // ── 半自动篮子确认 (v0.16.0) ──
    void pendingBasketChanged();

    void strategiesChanged();
    void created(const QString& strategyId, const QVariantMap& strategyData);
    void updated(const QString& strategyId);
    void deleted(const QString& strategyId);
    void started(const QString& strategyId);
    void stopped(const QString& strategyId);
    void operationFailed(int code, const QString& message);

private:
    static constexpr int kInvalidArgumentCode = 1001;
    static constexpr int kRepositoryErrorCode = 2001;
    static constexpr const char* kStrategyIdKey = "strategyId";

    struct StrategyTypeSpec final {
        domain::strategies::StrategyType value{domain::strategies::StrategyType::DOUBLE_MOVING_AVERAGE};
        bool valid{false};
    };

    struct StrategyBehaviorKindSpec final {
        domain::strategies::StrategyBehaviorKind value{domain::strategies::StrategyBehaviorKind::Custom};
        bool valid{false};
    };

    struct FactorIdListSpec final {
        std::vector<std::string> values;  // instance_id 字符串
        bool valid{true};
        bool provided{false};
    };

    struct RuleIdListSpec final {
        std::vector<domain::strategies::RuleId> values;
        bool valid{true};
        bool provided{false};
    };

    class BridgeUpsertRequest final {
    public:
        class CommonConfigPayload final {
        public:
            [[nodiscard]] bool allowShort() const noexcept { return allowShort_; }
            [[nodiscard]] int maxPositions() const noexcept { return maxPositions_; }
            [[nodiscard]] double maxWeightPerStock() const noexcept { return maxWeightPerStock_; }
            [[nodiscard]] double minWeightPerStock() const noexcept { return minWeightPerStock_; }
            [[nodiscard]] int weightScheme() const noexcept { return weightScheme_; }
            [[nodiscard]] int rebalanceFrequency() const noexcept { return rebalanceFrequency_; }
            void setAllowShort(bool value) { allowShort_ = value; }
            void setMaxPositions(int value) { maxPositions_ = value; }
            void setMaxWeightPerStock(double value) { maxWeightPerStock_ = value; }
            void setMinWeightPerStock(double value) { minWeightPerStock_ = value; }
            void setWeightScheme(int value) { weightScheme_ = value; }
            void setRebalanceFrequency(int value) { rebalanceFrequency_ = value; }
        private:
            bool allowShort_{false};
            int maxPositions_{100};
            double maxWeightPerStock_{0.1};
            double minWeightPerStock_{0.0};
            int weightScheme_{0};
            int rebalanceFrequency_{0};
        };

        class StrategySpecPayload final {
        public:
            [[nodiscard]] const QVariantMap& values() const noexcept { return values_; }
            void setValue(const QString& key, const QVariant& value) { values_.insert(key, value); }
        private:
            QVariantMap values_;
        };

        [[nodiscard]] const domain::strategies::StrategyUuid& strategyId() const noexcept { return strategyId_; }
        [[nodiscard]] bool hasStrategyId() const noexcept { return hasStrategyId_; }
        [[nodiscard]] const std::string& strategyName() const noexcept { return strategyName_; }
        [[nodiscard]] const std::string& description() const noexcept { return description_; }
        [[nodiscard]] const StrategyTypeSpec& strategyType() const noexcept { return strategyType_; }
        [[nodiscard]] const StrategyBehaviorKindSpec& behaviorKind() const noexcept { return behaviorKind_; }
        [[nodiscard]] const FactorIdListSpec& factorIds() const noexcept { return factorIds_; }
        [[nodiscard]] const RuleIdListSpec& ruleIds() const noexcept { return ruleIds_; }
        [[nodiscard]] bool status() const noexcept { return status_; }
        [[nodiscard]] const QVariantMap& parameters() const noexcept { return parameters_; }
        void setStrategyId(const domain::strategies::StrategyUuid& value) { strategyId_ = value; hasStrategyId_ = true; }
        void setStrategyName(std::string value) { strategyName_ = std::move(value); }
        void setDescription(std::string value) { description_ = std::move(value); }
        void setStrategyType(const StrategyTypeSpec& value) { strategyType_ = value; }
        void setBehaviorKind(const StrategyBehaviorKindSpec& value) { behaviorKind_ = value; }
        void setFactorIds(const FactorIdListSpec& value) { factorIds_ = value; }
        void setRuleIds(const RuleIdListSpec& value) { ruleIds_ = value; }
        void setStatus(bool value) { status_ = value; }
        void setParameters(const QVariantMap& value) { parameters_ = value; }
    private:
        domain::strategies::StrategyUuid strategyId_{foundation::utils::Uuid::null()};
        bool hasStrategyId_{false};
        std::string strategyName_;
        std::string description_;
        StrategyTypeSpec strategyType_;
        StrategyBehaviorKindSpec behaviorKind_;
        FactorIdListSpec factorIds_;
        RuleIdListSpec ruleIds_;
        bool status_{false};
        QVariantMap parameters_;
    };

    QString readText(const QVariantMap& payload, std::initializer_list<const char*> keys) const;
    bool isTypeIdxValid(int index) const;
    StrategyTypeSpec readTypeSpec(const QVariantMap& payload) const;
    StrategyBehaviorKindSpec readBehaviorKindSpec(const QVariantMap& payload) const;
    FactorIdListSpec readFactorIds(const QVariantMap& payload) const;
    RuleIdListSpec readRuleIds(const QVariantMap& payload) const;
    bool hasForbiddenFields(const QVariantMap& payload) const;
    QVariant readValue(const QVariantMap& payload, std::initializer_list<const char*> keys) const;
    QVariantMap readMap(const QVariantMap& payload, std::initializer_list<const char*> keys) const;
    std::optional<domain::strategies::StrategyUuid> readId(const QVariantMap& payload) const;
    BridgeUpsertRequest parseReq(const QVariantMap& payload) const;
    void applyReq(const BridgeUpsertRequest& request, astock::database::PersistedStrategyData& target) const;
    std::optional<domain::strategies::StrategyUuid> parseId(const QString& input) const;
    QString clearedMsg() const;

    void setBusy(bool busy);
    void setErr(const QString& message);
    void refreshModel();

    bool m_busy{false};
    bool m_inited{false};
    bool m_cacheOk{false};
    QString m_err;
    QString m_selId;
    std::unique_ptr<class astock::database::StrategyRepository> m_repo;
    StrategyListModel* m_listModel{nullptr};

    // 异步启动线程池（仅用于将工作从主线程卸到后台）
    std::unique_ptr<foundation::thread::ThreadPoolExecutor> m_startupPool;

    // 策略运行时状态（内存单向控制，不查 DB/引擎）
    QHash<QString, QString> m_runtimeStatus;

    // ── 半自动篮子确认 (v0.16.0) ──

    /// @brief IBasketInterceptor 实现: 引擎线程 → Qt 主线程
    bool onBasketReady(std::uint64_t basketId,
                      const std::vector<domain::strategy::OrderRequest>& orders,
                      const std::string& strategyId,
                      const std::string& strategyName,
                      const std::string& contextDescription) override;

    std::uint64_t m_pendingBasketId{0};
    QString m_pendingStrategyId;     ///< UUID, 用于 confirmBasket/rejectBasket 查引擎
    QString m_pendingStrategyName;   ///< 显示名, 用于 QML 弹窗标题
    QString m_pendingContextDesc;
    QVariantList m_pendingBasketOrders;  ///< QML 显示用订单列表
    std::vector<domain::strategy::OrderRequest> m_pendingOriginalOrders;  ///< 原始订单副本, 供 confirmBasket 重建

    /// @brief OrderRequest → QVariantList (跨线程传递到 QML)
    static QVariantList ordersToVariantList(const std::vector<domain::strategy::OrderRequest>& orders);

    /// @brief QVariantList → OrderRequest[] (QML 回传, 仅 quantity 可被编辑)
    static std::vector<domain::strategy::OrderRequest> variantListToOrders(
        const QVariantList& editedList,
        const std::vector<domain::strategy::OrderRequest>& originalOrders);

    static StrategyBridge* s_instance;
};
