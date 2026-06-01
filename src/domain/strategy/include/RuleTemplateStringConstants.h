#pragma once

namespace domain::strategy::rule_template_strings {

inline constexpr const char* kPathRulesExamples = "astock_engine/rules/examples";

inline constexpr const char* kBindingFilePath = "file_path";
inline constexpr const char* kBindingFileName = "file_name";
inline constexpr const char* kBindingTemplateId = "template_id";
inline constexpr const char* kBindingGroupId = "group_id";
inline constexpr const char* kBindingGroupTitle = "group_title";
inline constexpr const char* kBindingGroupRole = "group_role";
inline constexpr const char* kBindingGroupOperator = "group_operator";
inline constexpr const char* kBindingGroupMinMatchCount = "group_min_match_count";
inline constexpr const char* kBindingPhase = "bindingPhase";
inline constexpr const char* kCompiledTemplateBinding = "_binding";
inline constexpr const char* kCompiledTemplateFilePath = "_filePath";
inline constexpr const char* kCompiledTemplateLastModifiedMs = "_lastModifiedMs";

inline constexpr const char* kComposerState = "ruleComposerState";
inline constexpr const char* kComposerStages = "stages";
inline constexpr const char* kComposerGroups = "groups";
inline constexpr const char* kComposerGroupId = "groupId";
inline constexpr const char* kComposerTitle = "title";
inline constexpr const char* kComposerRole = "role";
inline constexpr const char* kComposerOperator = "operator";
inline constexpr const char* kComposerGroupMinMatchCount = "groupMinMatchCount";
inline constexpr const char* kComposerMinimumMatchCount = "minimumMatchCount";
inline constexpr const char* kComposerFilePath = "filePath";
inline constexpr const char* kComposerFileName = "fileName";
inline constexpr const char* kComposerTemplateId = "templateId";

inline constexpr const char* kScopeCandidate = "candidate";
inline constexpr const char* kScopeStrategy = "strategy";
inline constexpr const char* kScopeSymbol = "symbol";
inline constexpr const char* kSnapshotRuleProfile = "ruleProfileSnapshot";
inline constexpr const char* kSnapshotExecutionPolicy = "executionPolicySnapshot";
inline constexpr const char* kSnapshotStrategyScopeContext = "strategyScopeContextSnapshot";

inline constexpr const char* kConditionVar = "var";
inline constexpr const char* kConditionOp = "op";
inline constexpr const char* kConditionConditions = "conditions";
inline constexpr const char* kConditionValue = "value";
inline constexpr const char* kConditionNotCondition = "condition";
inline constexpr const char* kConditionLeft = "left";
inline constexpr const char* kConditionRight = "right";
inline constexpr const char* kConditionTruthy = "truthy";
inline constexpr const char* kConditionNot = "not";
inline constexpr const char* kConditionEq = "eq";
inline constexpr const char* kConditionNe = "ne";
inline constexpr const char* kConditionLt = "lt";
inline constexpr const char* kConditionLe = "le";
inline constexpr const char* kConditionGt = "gt";
inline constexpr const char* kConditionGe = "ge";
inline constexpr const char* kBooleanTrue = "true";
inline constexpr const char* kBooleanFalse = "false";
inline constexpr const char* kNumericZero = "0";

inline constexpr const char* kFactPrefixCandidateDot = "candidate.";
inline constexpr const char* kFactPrefixMarketDot = "market.";
inline constexpr const char* kFactPrefixStrategyDot = "strategy.";

inline constexpr const char* kFactPrefixes[] = {
    kFactPrefixCandidateDot,
    kFactPrefixMarketDot,
    kFactPrefixStrategyDot
};

inline constexpr const char* kFieldAllowActions = "allow_actions";
inline constexpr const char* kFieldResult = "result";
inline constexpr const char* kFieldPayload = "payload";
inline constexpr const char* kFieldState = "state";
inline constexpr const char* kFieldScore = "score";
inline constexpr const char* kFieldRules = "rules";
inline constexpr const char* kFieldWhen = "when";
inline constexpr const char* kFieldThen = "then";
inline constexpr const char* kFieldStage = "stage";
inline constexpr const char* kFieldPriority = "priority";
inline constexpr const char* kFieldId = "id";
inline constexpr const char* kFieldReasonCode = "reason_code";
inline constexpr const char* kFieldMessage = "message";
inline constexpr const char* kFieldNamespace = "namespace";

inline constexpr const char* kScopeMarket = "market";
inline constexpr const char* kScopeStrategyId = "strategy_id";
inline constexpr const char* kScopeParameters = "parameters";
inline constexpr const char* kScopeRuleProfile = "rule_profile";
inline constexpr const char* kScopeExecutionPolicy = "execution_policy";
inline constexpr const char* kScopeStrategyScopeContext = "strategy_scope_context";
inline constexpr const char* kScopeRuntimeSession = "runtime_session";
inline constexpr const char* kScopeLatestPrice = "latest_price";
inline constexpr const char* kScopeReferencePrice = "reference_price";
inline constexpr const char* kScopeMarketEventType = "market_event_type";
inline constexpr const char* kScopeCandidateAction = "candidate_action";
inline constexpr const char* kScopeCandidateStrength = "candidate_strength";

inline constexpr const char* kRuntimeSessionCash = "cash";
inline constexpr const char* kRuntimeSessionHasPosition = "hasPosition";
inline constexpr const char* kRuntimeSessionPositionQuantity = "positionQuantity";
inline constexpr const char* kRuntimeSessionEntryPrice = "entryPrice";
inline constexpr const char* kRuntimeSessionHoldingDays = "holdingDays";

inline constexpr const char* kCandidateFactHasPosition = "candidate.has_position";
inline constexpr const char* kCandidateFactPositionQuantity = "candidate.position_quantity";
inline constexpr const char* kCandidateFactEntryPrice = "candidate.entry_price";
inline constexpr const char* kCandidateFactPriceChangeRatio = "candidate.price_change_ratio";
inline constexpr const char* kCandidateFactPnlRatio = "candidate.pnl_ratio";

inline constexpr const char* kActionBuy = "buy";
inline constexpr const char* kActionSell = "sell";
inline constexpr const char* kActionEntry = "entry";
inline constexpr const char* kActionCandidateEntry = "candidate_entry";
inline constexpr const char* kActionOpen = "open";
inline constexpr const char* kActionReduce = "reduce";
inline constexpr const char* kActionExit = "exit";
inline constexpr const char* kActionClose = "close";

inline constexpr const char* kResultPass = "pass";
inline constexpr const char* kResultStateSwitch = "state_switch";
inline constexpr const char* kResultHalt = "halt";
inline constexpr const char* kResultBlock = "block";

inline constexpr const char* kStageSignal = "signal";
inline constexpr const char* kStageRebalance = "rebalance";
inline constexpr const char* kStageRisk = "risk";
inline constexpr const char* kStageWatch = "watch";
inline constexpr const char* kStageEligibility = "eligibility";
inline constexpr const char* kStagePortfolio = "portfolio";
inline constexpr const char* kStageExecution = "execution";
inline constexpr const char* kStageAccountRisk = "account_risk";

inline constexpr const char* kGroupOperatorAll = "all";
inline constexpr const char* kGroupOperatorAny = "any";
inline constexpr const char* kGroupOperatorAtLeast = "at_least";
inline constexpr const char* kGroupOperatorScoreSum = "score_sum";
inline constexpr const char* kGroupOperatorFirstMatch = "first_match";

inline constexpr const char* kGroupRoleMustPass = "must_pass";
inline constexpr const char* kGroupRoleEntryGuard = "entry_guard";
inline constexpr const char* kGroupRoleAnyPass = "any_pass";
inline constexpr const char* kGroupRoleTrigger = "trigger";
inline constexpr const char* kGroupRoleExitGuard = "exit_guard";
inline constexpr const char* kGroupRoleScoreBoost = "score_boost";
inline constexpr const char* kGroupRolePositionManagement = "position_management";

inline constexpr const char* kApplicabilityStageFiltered = "stage_filtered";
inline constexpr const char* kApplicabilityRoleFiltered = "role_filtered";

inline constexpr const char* kDecisionDispositionSkipped = "skipped";
inline constexpr const char* kDecisionDispositionConsidered = "considered";
inline constexpr const char* kDecisionOutcomeNotApplicable = "not_applicable";
inline constexpr const char* kDecisionOutcomeNotMatched = "not_matched";
inline constexpr const char* kDecisionOutcomeIncomplete = "incomplete";
inline constexpr const char* kDecisionOutcomeMatched = "matched";
inline constexpr const char* kDecisionReasonGroupIncomplete = "group_incomplete";
inline constexpr const char* kDecisionReasonGroupThresholdUnmet = "group_threshold_unmet";

inline constexpr const char* kDecisionFieldGroupTitle = "groupTitle";
inline constexpr const char* kDecisionFieldGroupRole = "groupRole";
inline constexpr const char* kDecisionFieldGroupOperator = "groupOperator";
inline constexpr const char* kDecisionFieldMemberCount = "memberCount";
inline constexpr const char* kDecisionFieldApplicableCount = "applicableCount";
inline constexpr const char* kDecisionFieldMatchedCount = "matchedCount";
inline constexpr const char* kDecisionFieldFilteredCount = "filteredCount";
inline constexpr const char* kDecisionFieldMatchThreshold = "matchThreshold";
inline constexpr const char* kDecisionFieldAggregatedScore = "aggregatedScore";
inline constexpr const char* kDecisionFieldDisposition = "disposition";
inline constexpr const char* kDecisionFieldOutcome = "outcome";
inline constexpr const char* kDecisionFieldSkipReason = "skipReason";
inline constexpr const char* kDecisionFieldMatchedRuleId = "matchedRuleId";
inline constexpr const char* kDecisionFieldMatchedResultType = "matchedResultType";
inline constexpr const char* kDecisionFieldMatchedReasonCode = "matchedReasonCode";
inline constexpr const char* kDecisionFieldSelectedBy = "selectedBy";

inline constexpr const char* kRuleTemplateEventTypeEntryBlock = "entry_block";
inline constexpr const char* kRuleTemplateEventTypeForcedExit = "forced_exit";

inline constexpr const char* kRuntimeReasonMustPassUnmet = "runtime_rule_template_must_pass_unmet";
inline constexpr const char* kRuntimeReasonAnyPassUnmet = "runtime_rule_template_any_pass_unmet";
inline constexpr const char* kPayloadSelectionScore = "selectionScore";
inline constexpr const char* kPayloadRuleSelectionScore = "ruleSelectionScore";
inline constexpr const char* kContextRuleTemplateJson = "rule_template_context_json";
inline constexpr const char* kSeparatorGroupKey = "|";
inline constexpr const char* kSeparatorScopeDisplay = " / ";
inline constexpr const char* kSeparatorDot = ".";

} // namespace domain::strategy::rule_template_strings