#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "factor_enums.h"

namespace factor {

class SentimentFactor final : public BaseFactor {
public:
    struct Params : CommonParams {
        SentimentMetric sentimentMetric{SentimentMetric::UNKNOWN};
        SentimentSource sentimentSource{SentimentSource::UNKNOWN};

        void fromJson(const foundation::json::JsonFacade& json);
    };

    SentimentFactor();

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;

    static std::shared_ptr<SentimentFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

    static constexpr const char* FIELD_SENTIMENT_SCORE = "sentiment_score";
    static constexpr const char* FIELD_SOCIAL_SENTIMENT = "social_sentiment";
    static constexpr const char* FIELD_INVESTOR_SENTIMENT = "investor_sentiment";
    static constexpr const char* FIELD_MARKET_SENTIMENT = "market_sentiment";

private:
    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor