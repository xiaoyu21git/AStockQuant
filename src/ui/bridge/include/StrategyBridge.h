#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "StrategyLifecycleStatus.h"
#include "../../domain/backtest/include/ResolvedStrategyBehavior.h"
#include "../../domain/strategies/include/StrategyDefinitionTypes.h"

#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class StrategyListModel;

namespace astock::database {
class IStrategyRepository;
struct PersistedStrategyData;
}

class StrategyBridge : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errMsg READ errMsg NOTIFY errMsgChanged)
    Q_PROPERTY(bool inited READ inited NOTIFY initedChanged)
    Q_PROPERTY(bool cacheOk READ cacheOk NOTIFY cacheOkChanged)
    Q_PROPERTY(QString selId READ selId WRITE setSelId NOTIFY selIdChanged)
    Q_PROPERTY(QAbstractListModel* listModel READ listModel CONSTANT)

public:
    explicit StrategyBridge(QObject* parent = nullptr);
    ~StrategyBridge() override;

    Q_INVOKABLE void init();
    Q_INVOKABLE void initAsync();
    Q_INVOKABLE bool inited() const;
    Q_INVOKABLE bool cacheOk() const;

    // FROZEN CRUD INTERFACE CONTRACT (2026-05-31)
    // - add/update/remove/get signatures must remain unchanged.
    // - add/update payload keys are explicitly frozen and validated in bridge runtime.
    // - strategyId for update/remove/get must be UUID and no fallback alias is allowed.
    Q_INVOKABLE QString add(const QVariantMap& payload);
    Q_INVOKABLE bool update(const QVariantMap& payload);
    Q_INVOKABLE bool remove(const QString& strategyId);
    Q_INVOKABLE QVariantMap get(const QString& strategyId);
    Q_INVOKABLE QVariantList list();
    Q_INVOKABLE bool start(const QString& strategyId);
    Q_INVOKABLE bool stop(const QString& strategyId);
    Q_INVOKABLE bool saveViewCfg(const QString& strategyId, const QVariantMap& visualConfig);

    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString errMsg() const;
    [[nodiscard]] QString selId() const;
    [[nodiscard]] QAbstractListModel* listModel() const;
    void setSelId(const QString& strategyId);

signals:
    void busyChanged();
    void errMsgChanged();
    void initedChanged();
    void cacheOkChanged();
    void selIdChanged();

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
        std::vector<domain::strategies::FactorId> values;
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

            [[nodiscard]] QVariantMap toVariantMap() const
            {
                QVariantMap map;
                map.insert(QStringLiteral("allowShort"), allowShort_);
                map.insert(QStringLiteral("maxPositions"), maxPositions_);
                map.insert(QStringLiteral("maxWeightPerStock"), maxWeightPerStock_);
                map.insert(QStringLiteral("minWeightPerStock"), minWeightPerStock_);
                map.insert(QStringLiteral("weightScheme"), weightScheme_);
                map.insert(QStringLiteral("rebalanceFrequency"), rebalanceFrequency_);
                return map;
            }

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
            void setValue(const QString& key, const QVariant& value)
            {
                if (!value.isValid() || value.isNull()) {
                    return;
                }
                values_.insert(key, value);
            }

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
        [[nodiscard]] const CommonConfigPayload& commonConfig() const noexcept { return commonConfig_; }
        [[nodiscard]] const StrategySpecPayload& strategySpec() const noexcept { return strategySpec_; }

        void setStrategyId(const domain::strategies::StrategyUuid& value)
        {
            strategyId_ = value;
            hasStrategyId_ = true;
        }

        void setStrategyName(std::string value) { strategyName_ = std::move(value); }
        void setDescription(std::string value) { description_ = std::move(value); }
        void setStrategyType(const StrategyTypeSpec& value) { strategyType_ = value; }
        void setBehaviorKind(const StrategyBehaviorKindSpec& value) { behaviorKind_ = value; }
        void setFactorIds(const FactorIdListSpec& value) { factorIds_ = value; }
        void setRuleIds(const RuleIdListSpec& value) { ruleIds_ = value; }
        void setStatus(bool value) { status_ = value; }
        void setParameters(const QVariantMap& value) { parameters_ = value; }
        void setCommonConfig(const CommonConfigPayload& value) { commonConfig_ = value; }
        void setStrategySpec(const StrategySpecPayload& value) { strategySpec_ = value; }

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
        CommonConfigPayload commonConfig_;
        StrategySpecPayload strategySpec_;
    };

    QString readText(const QVariantMap& payload, std::initializer_list<const char*> keys) const;
    bool isTypeIdxValid(int index) const;
    StrategyTypeSpec readTypeSpec(const QVariantMap& payload) const;
    StrategyBehaviorKindSpec readBehaviorKindSpec(const QVariantMap& payload) const;
    FactorIdListSpec readFactorIds(const QVariantMap& payload) const;
    RuleIdListSpec readRuleIds(const QVariantMap& payload) const;
    bool hasForbiddenFields(const QVariantMap& payload) const;
    QVariant readValue(const QVariantMap& payload,
                       std::initializer_list<const char*> keys) const;
    QVariantMap readMap(const QVariantMap& payload,
                        std::initializer_list<const char*> keys) const;
    std::optional<domain::strategies::StrategyUuid> readId(const QVariantMap& payload) const;
    BridgeUpsertRequest::CommonConfigPayload readCommonConfig(const QVariantMap& parameters) const;
    BridgeUpsertRequest::StrategySpecPayload readStrategySpecPayload(
        const StrategyTypeSpec& strategyType,
        const QVariantMap& parameters) const;
    BridgeUpsertRequest parseReq(const QVariantMap& payload) const;
   
    void applyReq(const BridgeUpsertRequest& request,
                  astock::database::PersistedStrategyData& target) const;
    std::optional<domain::strategies::StrategyUuid> parseId(const QString& input) const;
    QString clearedMsg() const;

    bool needId(const QString& strategyId, int code, const QString& messagePrefix);
    void setBusy(bool busy);
    void setErr(const QString& message);
    void refreshModel();

    bool m_busy{false};
    bool m_inited{false};
    bool m_cacheOk{false};
    QString m_err;
    QString m_selId;
    std::unique_ptr<astock::database::IStrategyRepository> m_repo;
    StrategyListModel* m_listModel{nullptr};
};
