#pragma once
#include <QObject>
#include <QVariantMap>
#include <QVariantList>

class StrategyParamConfigHelper : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap StrategyTypeIndex    READ strategyTypeIndex    CONSTANT)
    Q_PROPERTY(QVariantMap StrategyBehaviorKind READ strategyBehaviorKind CONSTANT)
    Q_PROPERTY(QVariantMap AssetTypeIndex       READ assetTypeIndex       CONSTANT)
    Q_PROPERTY(QVariantMap TimeFrameIndex        READ timeFrameIndex        CONSTANT)
    Q_PROPERTY(QVariantMap RiskLevelIndex        READ riskLevelIndex        CONSTANT)
    Q_PROPERTY(QVariantMap StrategyStoredTypeIndex READ strategyTypeIndex CONSTANT)

public:
    explicit StrategyParamConfigHelper(QObject* parent = nullptr);
    QVariantMap strategyTypeIndex()    const { return m_strategyTypeIndex; }
    QVariantMap strategyBehaviorKind() const { return m_strategyBehaviorKind; }
    QVariantMap assetTypeIndex()       const { return m_assetTypeIndex; }
    QVariantMap timeFrameIndex()        const { return m_timeFrameIndex; }
    QVariantMap riskLevelIndex()        const { return m_riskLevelIndex; }

    // i18n
    Q_INVOKABLE QString tr(const QString& key) const;

    // ── 类型映射 (新名 + 旧名兼容) ──
    Q_INVOKABLE int normalizeStrategyTypeIndex(int v) const;
    Q_INVOKABLE int strategyBehaviorKindFromTypeIndex(int idx) const;
    Q_INVOKABLE int strategyTypeIndexFromBehaviorKind(int bk) const;
    Q_INVOKABLE QString getStrategyTypeNameFromIndex(int idx) const;
    Q_INVOKABLE QString getStrategyTypeDescriptionFromIndex(int idx) const;
    Q_INVOKABLE QString getStrategyIconFromIndex(int idx) const;
    Q_INVOKABLE QString getBriefDescriptionFromIndex(int idx) const;
    Q_INVOKABLE QString getDefaultStrategyDescriptionFromIndex(int idx) const;
    Q_INVOKABLE QVariantList getDefaultStrategyTagsFromIndex(int idx) const;
    Q_INVOKABLE QString strategyBehaviorKindLabel(int bk) const;
    Q_INVOKABLE QString getStrategyBehaviorKindLabelFromIndex(int idx) const;

    // ── 资产/时间/风险 ──
    Q_INVOKABLE QString getAssetTypeNameFromIndex(int idx) const;
    Q_INVOKABLE QString getTimeFrameNameFromIndex(int idx) const;
    Q_INVOKABLE QString getRiskLevelNameFromIndex(int idx) const;
    Q_INVOKABLE QString getRiskLevelColorFromIndex(int idx) const;

    // ── 核心 ──
    Q_INVOKABLE QVariantList buildParamConfigs(int idx) const;
    Q_INVOKABLE QVariantMap normalizeStrategyParameters(int idx, const QVariantMap& raw) const;
    Q_INVOKABLE QVariantMap buildCompleteStrategyData(const QVariantMap& ctx) const;
    Q_INVOKABLE QVariantMap buildDefaultStrategyProfile(int idx) const;

    // ── 步骤 ──
    Q_INVOKABLE QString stepLabel(int s) const;
    Q_INVOKABLE QString stepTitle(int s) const;
    Q_INVOKABLE QString stepDescription(int s) const;
    Q_INVOKABLE bool isStepValid(int s, const QVariantMap& d) const;

    // ── 规则模板 ──
    Q_INVOKABLE QVariantMap getRuleComposerStageDefinitions() const;
    Q_INVOKABLE QString resolveRuleTemplateFileName(const QString& name) const;
    Q_INVOKABLE QVariantMap buildDefaultBaseRuleBindings(int idx) const;
    Q_INVOKABLE QVariantMap buildDefaultMarketRuleBindings(int idx) const;
    Q_INVOKABLE QVariantMap buildDefaultRuleComposerSkeleton(int idx) const;
    Q_INVOKABLE QVariantMap validateRuleComposerConfiguration(const QVariantMap& state) const;
    Q_INVOKABLE QVariantMap resetFormData() const;

    // ── 旧名兼容 ──
    Q_INVOKABLE QString getRiskLevelName(int idx) const;
    Q_INVOKABLE QString getDefaultStrategyDescription(int idx) const;
    Q_INVOKABLE QVariantList getDefaultStrategyTags(int idx) const;

private:
    static QVariantMap makeEnum(std::initializer_list<std::pair<const char*, int>> vals);
    QVariantMap m_strategyTypeIndex;
    QVariantMap m_strategyBehaviorKind;
    QVariantMap m_assetTypeIndex;
    QVariantMap m_timeFrameIndex;
    QVariantMap m_riskLevelIndex;
};
