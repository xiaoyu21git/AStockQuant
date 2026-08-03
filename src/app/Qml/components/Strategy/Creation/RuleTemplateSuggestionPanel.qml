import AStock.Bridge 1.0 as Bridge
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import "../../../utils/RuleTemplatePreviewUtils.js" as PreviewUtils
import "../../Base" as BaseComponents

Rectangle {
    id: root

    property string panelTitle: "规则模板建议"
    property string phaseLockValue: ""
    property string queryPlaceholderText: ""
    property bool showInlinePhaseInputs: false
    property int selectedStrategyTypeIndex: -1
    property int selectedStrategyBehaviorKind: -1
    property var strategyProfile: ({})
    property string selectedStageId: ""
    property string selectedStageTitle: ""
    property string selectedGroupId: ""
    property string selectedGroupTitle: ""
    property string selectedGroupRole: ""
    property string activeRequestId: ""
    property string resolvedTermId: ""
    property string resolvedTermDisplayName: ""
    property string errorMessage: ""
    property string hintMessage: "输入一个交易术语，查看系统可复用的规则模板与缺失特征。"
    property string applyFeedbackMessage: ""
    property string applyFeedbackTone: "success"
    property var suggestionItems: []
    property var inlinePhaseDrafts: ({ signal: "", rebalance: "", market: "" })
    property string inlineActivePhase: "signal"
    property bool hasSearched: false
    property bool onlyReady: false
    readonly property bool requestInFlight: activeRequestId !== ""
    readonly property bool phaseLocked: Bridge.StrategyBridge.normalizePhaseKey(phaseLockValue) !== ""
    readonly property string contextualPhaseValue: suggestionPhaseForStage(selectedStageId)
    readonly property bool compactInlineLayout: showInlinePhaseInputs && width >= 520
    readonly property bool compactSuggestionActionRow: width < 360
    readonly property int currentStrategyBehaviorKind: resolveCurrentStrategyBehaviorKind()
    readonly property var currentStepTypeSpec: resolveCurrentStepTypeSpec()
    readonly property var currentStrategyTypeSpec: resolveCurrentStrategyTypeSpec()
    readonly property var stepTypeMatchedSuggestionItems: filterSuggestionsByCurrentStepType(suggestionItems)
    readonly property var strategyTypeMatchedSuggestionItems: filterSuggestionsByStrategyType(suggestionItems)
    readonly property var surfacedSuggestionItems: sortSuggestionItems(
        strategyTypeMatchedSuggestionItems.length > 0 ? strategyTypeMatchedSuggestionItems : suggestionItems)
    readonly property var groupedSuggestionSections: groupSuggestionItems(surfacedSuggestionItems)
    readonly property int readySuggestionCount: countReadySuggestions(surfacedSuggestionItems)
    readonly property var ruleTemplateSuggestionService: Bridge.RuleTemplateSuggestionService
    readonly property var phaseOptions: [
        { label: "全部阶段", value: "" },
        { label: "入场/观察信号", value: "signal" },
        { label: "持仓管理/退出", value: "rebalance" },
        { label: "市场/风控", value: "market" }
    ]
    readonly property var quickQueries: [
        { label: "弱转强", phase: "signal", roles: ["must_pass", "any_pass", "score_boost"] },
        { label: "炸板回封", phase: "signal", roles: ["must_pass", "any_pass", "score_boost"] },
        { label: "卡位上位", phase: "signal", roles: ["must_pass", "any_pass", "score_boost"] },
        { label: "补涨突破", phase: "signal", roles: ["must_pass", "any_pass", "score_boost"] },
        { label: "情绪修复回流", phase: "signal", roles: ["must_pass", "any_pass", "score_boost"] },
        { label: "回踩20日线", phase: "signal", roles: ["must_pass", "any_pass", "score_boost"] },
        { label: "回踩60日线", phase: "signal", roles: ["must_pass", "any_pass", "score_boost"] },
        { label: "低位首板隔日确认", phase: "signal", roles: ["must_pass", "any_pass", "score_boost"] },
        { label: "板块回流跟涨确认", phase: "signal", roles: ["must_pass", "any_pass", "score_boost"] },
        { label: "中期平台突破", phase: "signal", roles: ["must_pass", "any_pass", "score_boost"] },
        { label: "年线收复回踩确认", phase: "signal", roles: ["must_pass", "any_pass", "score_boost"] },
        { label: "业绩超预期突破", phase: "signal", roles: ["must_pass", "any_pass", "score_boost"] },
        { label: "盘口扫单回补确认", phase: "signal", roles: ["must_pass", "any_pass", "score_boost"] },
        { label: "情绪修复后午后反杀", phase: "signal", roles: ["veto"] },
        { label: "高位炸板次日缩量阴跌", phase: "signal", roles: ["veto"] },
        { label: "午后回封失败", phase: "signal", roles: ["veto"] },
        { label: "高开低走无承接", phase: "signal", roles: ["veto"] },
        { label: "冲板未遂回落", phase: "signal", roles: ["veto"] },
        { label: "跌破分时均线回拉失败", phase: "signal", roles: ["veto"] },
        { label: "分批止盈", phase: "rebalance", roles: ["position_management", "any_pass"] },
        { label: "反包失败退出", phase: "rebalance", roles: ["any_pass", "must_pass"] },
        { label: "承接走弱退出", phase: "rebalance", roles: ["any_pass", "position_management"] },
        { label: "弱转强失败后低开无承接", phase: "rebalance", roles: ["any_pass"] },
        { label: "跌破120日线退出", phase: "rebalance", roles: ["any_pass", "position_management"] },
        { label: "牛市趋势放行新开仓", phase: "market", roles: ["must_pass"] },
        { label: "震荡市精选放行", phase: "market", roles: ["must_pass"] },
        { label: "情绪修复放行新开仓", phase: "market", roles: ["must_pass"] },
        { label: "回封率修复放行新开仓", phase: "market", roles: ["must_pass"] },
        { label: "熊市冻结新开仓", phase: "market", roles: ["veto", "account_guard"] },
        { label: "情绪退潮冻结新开仓", phase: "market", roles: ["must_pass", "veto", "account_guard"] },
        { label: "高位炸板率恶化冻结新开仓", phase: "market", roles: ["veto", "account_guard"] },
        { label: "回封率下滑冻结新开仓", phase: "market", roles: ["veto", "account_guard"] },
        { label: "题材退潮冻结新开仓", phase: "market", roles: ["veto", "account_guard"] }
    ]
    readonly property var inlinePhasePanels: [
        {
            label: "入场/观察信号",
            value: "signal",
            placeholder: "例如：情绪修复后午后反杀",
            examples: [
                "弱转强",
                "炸板回封",
                "卡位上位",
                "补涨突破",
                "情绪修复回流",
                "回踩20日线",
                "回踩60日线",
                "中期平台突破",
                "年线收复回踩确认",
                "业绩超预期突破",
                "盘口扫单回补确认",
                "低位首板隔日确认",
                "板块回流跟涨确认",
                "情绪修复后午后反杀",
                "高位炸板次日缩量阴跌",
                "午后回封失败",
                "高开低走无承接",
                "冲板未遂回落",
                "跌破分时均线回拉失败"
            ]
        },
        {
            label: "持仓管理/退出",
            value: "rebalance",
            placeholder: "例如：弱转强失败后低开无承接",
            examples: [
                "分批止盈",
                "反包失败退出",
                "承接走弱退出",
                "跌破120日线退出",
                "炸板回落尾盘失守",
                "弱转强失败后低开无承接"
            ]
        },
        {
            label: "市场/风控",
            value: "market",
            placeholder: "例如：情绪退潮冻结新开仓",
            examples: [
                "牛市趋势放行新开仓",
                "震荡市精选放行",
                "情绪退潮冻结新开仓",
                "情绪修复放行新开仓",
                "回封率修复放行新开仓",
                "熊市冻结新开仓",
                "高位炸板率恶化冻结新开仓",
                "回封率下滑冻结新开仓",
                "题材退潮冻结新开仓"
            ]
        }
    ]
    readonly property var displayedInlinePhasePanels: {
        if (phaseLocked) {
            return inlinePhasePanels.filter(function(panelSpec) {
                return Bridge.StrategyBridge.normalizePhaseKey(panelSpec.value) === Bridge.StrategyBridge.normalizePhaseKey(phaseLockValue)
            })
        }
        if (contextualPhaseValue !== "") {
            return inlinePhasePanels.filter(function(panelSpec) {
                return Bridge.StrategyBridge.normalizePhaseKey(panelSpec.value) === contextualPhaseValue
            })
        }
        return inlinePhasePanels
    }
    readonly property var displayedQuickQueries: {
        var targetRole = String(selectedGroupRole || "").trim().toLowerCase()
        var targetPhase = phaseLocked
            ? Bridge.StrategyBridge.normalizePhaseKey(phaseLockValue)
            : contextualPhaseValue
        return quickQueries.filter(function(item) {
            if (targetPhase !== "" && Bridge.StrategyBridge.normalizePhaseKey(item.phase) !== targetPhase) {
                return false
            }
            if (!targetRole) {
                return true
            }
            var roles = normalizeList(item && item.roles)
            return roles.length === 0 || roles.indexOf(targetRole) >= 0
        })
    }

    signal applySuggestionRequested(var suggestion, string applyMode)

    implicitHeight: suggestionPanelLayout.implicitHeight + 24

    function roleDisplayName(role) {
        var mapping = {
            must_pass: "必须满足",
            any_pass: "任一满足",
            veto: "否决条件",
            score_boost: "评分增强",
            position_management: "仓位管理",
            execution_constraint: "执行限制",
            account_guard: "账户保护"
        }
        return mapping[role] || role || "未指定"
    }

    function resolvedStrategyLabel() {
        var normalizedStrategyTypeIndex = Bridge.StrategyBridge.normalizeStrategyTypeIndex(selectedStrategyTypeIndex)
        if (normalizedStrategyTypeIndex !== -1) {
            return Bridge.StrategyBridge.strategyTypeName(normalizedStrategyTypeIndex) || "当前策略"
        }
        return Bridge.StrategyBridge.strategyTypeName(currentStrategyBehaviorKind)
    }

    function resolveCurrentStrategyBehaviorKind() {
        var explicitKind = Number(selectedStrategyBehaviorKind)
        if (isFinite(explicitKind) && explicitKind >= 0 && explicitKind <= 8) {
            return Math.floor(explicitKind)
        }

        var profile = strategyProfile || ({})
        var parameters = profile.parameters || ({})
        explicitKind = Number(profile.strategyBehaviorKind !== undefined ? profile.strategyBehaviorKind : parameters.strategyBehaviorKind)
        if (isFinite(explicitKind) && explicitKind >= 0 && explicitKind <= 8) {
            return Math.floor(explicitKind)
        }

        explicitKind = Number(profile.strategy_behavior_kind !== undefined ? profile.strategy_behavior_kind : parameters.strategy_behavior_kind)
        if (isFinite(explicitKind) && explicitKind >= 0 && explicitKind <= 8) {
            return Math.floor(explicitKind)
        }

        return Bridge.StrategyBridge.strategyBehaviorKindFromTypeIndex(selectedStrategyTypeIndex)
    }

    function suggestionPhaseForStage(stageId) {
        var stageKey = String(stageId || "").trim().toLowerCase()
        if (stageKey === "market" || stageKey === "account_risk") {
            return "market"
        }
        if (stageKey === "rebalance" || stageKey === "portfolio" || stageKey === "execution") {
            return "rebalance"
        }
        if (stageKey === "signal" || stageKey === "eligibility") {
            return "signal"
        }
        return ""
    }

    function contextualActionFilter() {
        // 不在请求层按动作裁掉结果，避免当前组选中“否决/退出/风控”时看不到同阶段其它可用规则。
        // 当前步骤的强相关性改由前端排序和提示语处理。
        return ""
    }

    function normalizeTokenList(values) {
        var list = normalizeList(values)
        var normalized = []
        var seen = ({})
        for (var index = 0; index < list.length; ++index) {
            var token = String(list[index] || "").trim().toLowerCase()
            if (!token || seen[token]) {
                continue
            }
            seen[token] = true
            normalized.push(token)
        }
        return normalized
    }

    function normalizeSearchText(value) {
        return String(value || "")
            .trim()
            .toLowerCase()
            .replace(/[\s_-]+/g, "")
    }

    function intersectsNormalizedTokens(leftValues, rightValues) {
        var left = normalizeTokenList(leftValues)
        var right = normalizeTokenList(rightValues)
        if (left.length === 0 || right.length === 0) {
            return false
        }

        var rightLookup = ({})
        for (var rightIndex = 0; rightIndex < right.length; ++rightIndex) {
            rightLookup[right[rightIndex]] = true
        }
        for (var leftIndex = 0; leftIndex < left.length; ++leftIndex) {
            if (rightLookup[left[leftIndex]]) {
                return true
            }
        }
        return false
    }

    function resolveCurrentStepTypeSpec() {
        var stageKey = String(selectedStageId || "").trim().toLowerCase()
        var groupId = String(selectedGroupId || "").trim().toLowerCase()
        var roleKey = String(selectedGroupRole || "").trim().toLowerCase()

        if (groupId === "signal_core") {
            return { label: "核心确认组", categories: ["entry_pattern"], actions: ["candidate_entry", "open", "score"] }
        }
        if (groupId === "eligibility_core") {
            return { label: "基础过滤组", categories: ["eligibility_filter", "entry_pattern"], actions: ["pass", "candidate_entry", "open"] }
        }
        if (groupId === "signal_boost") {
            return { label: "评分增强组", categories: ["entry_pattern"], actions: ["score", "candidate_entry", "open"] }
        }
        if (groupId === "signal_veto") {
            return { label: "信号否决组", categories: ["watch_invalidation"], actions: ["block", "unwatch", "warn"] }
        }
        if (groupId === "rebalance_exit") {
            return { label: "退出触发组", categories: ["exit_pattern"], actions: ["exit", "reduce", "cooldown"] }
        }
        if (groupId === "rebalance_scale") {
            return { label: "分批管理组", categories: ["exit_management"], actions: ["reduce", "exit", "cooldown", "warn"] }
        }
        if (groupId === "market_gate") {
            return { label: "市场放行组", categories: ["market_gate", "market_risk"], actions: ["state_switch", "candidate_entry", "open", "freeze", "halt"] }
        }
        if (groupId === "market_veto") {
            return { label: "风险否决组", categories: ["market_risk"], actions: ["freeze", "halt", "state_switch"] }
        }
        if (groupId === "account_guard") {
            return { label: "账户保护组", categories: ["market_risk"], actions: ["freeze", "halt", "state_switch"] }
        }

        if (stageKey === "signal" && roleKey === "veto") {
            return { label: "信号否决", categories: ["watch_invalidation"], actions: ["block", "unwatch", "warn"] }
        }
        if (stageKey === "eligibility") {
            return { label: "基础过滤", categories: ["eligibility_filter", "entry_pattern"], actions: ["pass", "candidate_entry", "open"] }
        }
        if (stageKey === "signal" && roleKey === "score_boost") {
            return { label: "评分增强", categories: ["entry_pattern"], actions: ["score", "candidate_entry", "open"] }
        }
        if (stageKey === "signal") {
            return { label: "入场确认", categories: ["entry_pattern"], actions: ["candidate_entry", "open", "score"] }
        }
        if (stageKey === "rebalance" && roleKey === "position_management") {
            return { label: "持仓管理", categories: ["exit_management"], actions: ["reduce", "exit", "cooldown", "warn"] }
        }
        if (stageKey === "rebalance") {
            return { label: "退出触发", categories: ["exit_pattern"], actions: ["exit", "reduce", "cooldown"] }
        }
        if (stageKey === "market" || stageKey === "account_risk") {
            return { label: "市场风控", categories: ["market_risk"], actions: ["freeze", "halt", "state_switch"] }
        }

        return { label: "", categories: [], actions: [] }
    }

    function resolveCurrentStrategyTypeSpec() {
        var behaviorKind = currentStrategyBehaviorKind
        if (behaviorKind === 0) {
            return {
                label: resolvedStrategyLabel(),
                preferredTags: ["pullback", "ma20", "ma60", "breakout", "platform_breakout", "yearline", "long_term", "mid_term", "ma120", "take_profit", "scale_out", "acceptance", "breakdown", "market_state", "market_gate", "market_risk", "bull_market", "sideways_market", "bear_market", "eligibility", "liquidity", "trend", "veto"],
                preferredKeywords: ["回踩", "突破", "平台突破", "年线", "120日线", "趋势", "止盈", "承接走弱", "分批止盈", "市场", "风控", "放行", "牛市", "震荡市", "熊市冻结", "过滤", "资格", "流动性", "否决", "失守"],
                blockedTags: ["board", "reseal", "rotation", "overtake", "catch_up", "tail_ramp", "afternoon_chase", "counter_nuke", "afternoon_reseal", "first_board", "one_word_board", "spike_fail", "floor_to_limit", "afternoon_reversal_kill", "open_board"],
                preferredCategories: ["eligibility_filter", "entry_pattern", "market_gate", "market_risk", "watch_invalidation", "exit_pattern", "exit_management"],
                marketRegimePriority: ({ bull_market: 3, sideways_market: 2, bear_market: 1 }),
                marketBiasHint: "牛市趋势放行，其次展示震荡市精选放行，并保留熊市冻结"
            }
        }
        if (behaviorKind === 1) {
            return {
                label: resolvedStrategyLabel(),
                preferredTags: ["pullback", "ma20", "ma60", "rebound", "engulfing", "false_repair", "rebound_failed", "failed_rebound", "thin_volume", "acceptance", "breakdown", "market_gate", "market_risk", "sideways_market", "bear_market"],
                preferredKeywords: ["反包", "反抽", "修复", "回踩", "均线", "地量", "回归", "承接", "震荡市", "精选放行", "熊市冻结"],
                blockedTags: ["board", "reseal", "rotation", "overtake", "catch_up", "tail_ramp", "afternoon_chase", "counter_nuke", "afternoon_reseal", "first_board", "one_word_board", "spike_fail", "floor_to_limit", "open_board"],
                preferredCategories: ["entry_pattern", "market_gate", "market_risk", "exit_pattern"],
                marketRegimePriority: ({ sideways_market: 3, bear_market: 2, bull_market: 1 }),
                marketBiasHint: "震荡市精选放行，其次保留熊市冻结，再补充牛市放行"
            }
        }
        if (behaviorKind === 3 || behaviorKind === 4 || behaviorKind === 5) {
            return {
                label: resolvedStrategyLabel(),
                preferredTags: ["take_profit", "scale_out", "market_state", "market_gate", "market_risk", "breakdown", "acceptance", "long_term", "ma120", "bull_market", "sideways_market", "bear_market"],
                preferredKeywords: ["止盈", "风控", "冻结", "放行", "回撤", "流动性", "交易资格", "池子", "120日线", "牛市", "震荡市", "熊市"],
                blockedTags: ["board", "reseal", "rotation", "overtake", "catch_up", "tail_ramp", "afternoon_chase", "counter_nuke", "afternoon_reseal", "first_board", "one_word_board", "spike_fail", "floor_to_limit", "open_board", "emotion_repair"],
                preferredCategories: ["exit_management", "exit_pattern", "market_risk", "market_gate"],
                marketRegimePriority: ({ bear_market: 3, sideways_market: 2, bull_market: 2 }),
                marketBiasHint: "先看熊市冻结，再看震荡市精选放行和牛市放行"
            }
        }
        if (behaviorKind === 6) {
            return {
                label: resolvedStrategyLabel(),
                preferredTags: ["event_driven", "earnings", "breakout", "market_gate", "market_risk", "bull_market", "sideways_market", "bear_market"],
                preferredKeywords: ["业绩", "公告", "催化", "事件", "放量突破", "放行", "牛市", "震荡市", "熊市冻结"],
                blockedTags: ["ma20", "ma60", "yearline", "ma120", "tail_ramp", "counter_nuke"],
                preferredCategories: ["entry_pattern", "market_gate", "market_risk"],
                marketRegimePriority: ({ bull_market: 3, sideways_market: 2, bear_market: 1 }),
                marketBiasHint: "牛市放行优先，其次展示震荡市精选放行，并保留熊市冻结"
            }
        }
        if (behaviorKind === 7) {
            return {
                label: resolvedStrategyLabel(),
                preferredTags: ["high_frequency", "orderflow", "microstructure", "market_gate", "market_risk", "sideways_market", "bull_market", "bear_market"],
                preferredKeywords: ["盘口", "扫单", "回补", "微结构", "震荡市", "精选放行", "熊市冻结"],
                blockedTags: ["ma20", "ma60", "yearline", "ma120", "platform_breakout", "long_term"],
                preferredCategories: ["entry_pattern", "market_gate", "market_risk"],
                marketRegimePriority: ({ sideways_market: 3, bull_market: 2, bear_market: 1 }),
                marketBiasHint: "震荡市精选放行优先，其次展示牛市放行，并保留熊市冻结"
            }
        }
        return {
            label: resolvedStrategyLabel(),
            preferredTags: [],
            preferredKeywords: [],
            blockedTags: [],
            preferredCategories: [],
            marketRegimePriority: ({}),
            marketBiasHint: ""
        }
    }

    function itemStrategyTypeSortScore(item, spec) {
        var strategySpec = spec || currentStrategyTypeSpec
        if (!strategySpec) {
            return 0
        }

        var preferredTags = normalizeTokenList(strategySpec.preferredTags)
        var preferredCategories = normalizeTokenList(strategySpec.preferredCategories)
        var preferredKeywords = normalizeTokenList(strategySpec.preferredKeywords)
        var marketRegimePriority = (strategySpec && strategySpec.marketRegimePriority) || ({})
        var itemTags = normalizeTokenList(item && item.tags)
        var itemCategory = String((item && item.category) || "").trim().toLowerCase()
        var searchableText = suggestionSearchableText(item)
        var score = 0

        if (preferredCategories.indexOf(itemCategory) >= 0) {
            score += 12
        }

        for (var tagIndex = 0; tagIndex < itemTags.length; ++tagIndex) {
            var tag = itemTags[tagIndex]
            if (preferredTags.indexOf(tag) >= 0) {
                score += 4
            }
            if (marketRegimePriority[tag] !== undefined) {
                score += Number(marketRegimePriority[tag]) * 10
            }
        }

        for (var keywordIndex = 0; keywordIndex < preferredKeywords.length; ++keywordIndex) {
            if (searchableText.indexOf(preferredKeywords[keywordIndex]) >= 0) {
                score += 2
            }
        }

        return score
    }

    function suggestionSearchableText(item) {
        return normalizeSearchText([
            item && (item.template_display_name || item.templateDisplayName),
            item && (item.term_display_name || item.termDisplayName),
            item && item.summary,
            item && item.category,
            normalizeList(item && item.tags).join(" "),
            normalizeList(item && item.matched_aliases).join(" ")
        ].join(" "))
    }

    function itemMatchesCurrentStrategyType(item, spec) {
        var strategySpec = spec || currentStrategyTypeSpec
        var preferredTags = normalizeTokenList(strategySpec && strategySpec.preferredTags)
        var blockedTags = normalizeTokenList(strategySpec && strategySpec.blockedTags)
        var preferredCategories = normalizeTokenList(strategySpec && strategySpec.preferredCategories)
        var preferredKeywords = normalizeTokenList(strategySpec && strategySpec.preferredKeywords)

        if (preferredTags.length === 0
                && blockedTags.length === 0
                && preferredCategories.length === 0
                && preferredKeywords.length === 0) {
            return true
        }

        var itemTags = normalizeTokenList(item && item.tags)
        var itemCategory = String((item && item.category) || "").trim().toLowerCase()
        var searchableText = suggestionSearchableText(item)

        if (blockedTags.length > 0 && intersectsNormalizedTokens(itemTags, blockedTags)) {
            return false
        }
        if (preferredCategories.length > 0 && preferredCategories.indexOf(itemCategory) >= 0) {
            return true
        }
        if (preferredTags.length > 0 && intersectsNormalizedTokens(itemTags, preferredTags)) {
            return true
        }
        for (var keywordIndex = 0; keywordIndex < preferredKeywords.length; ++keywordIndex) {
            if (searchableText.indexOf(preferredKeywords[keywordIndex]) >= 0) {
                return true
            }
        }

        return false
    }

    function filterSuggestionsByStrategyType(items) {
        var list = normalizeList(items)
        var spec = currentStrategyTypeSpec
        if (!spec) {
            return list
        }

        var hasFiltering = normalizeTokenList(spec.preferredTags).length > 0
            || normalizeTokenList(spec.blockedTags).length > 0
            || normalizeTokenList(spec.preferredCategories).length > 0
            || normalizeTokenList(spec.preferredKeywords).length > 0
        if (!hasFiltering) {
            return list
        }

        return list.filter(function(item) {
            return itemMatchesCurrentStrategyType(item, spec)
        })
    }

    function itemMatchesCurrentStepType(item, spec) {
        var ruleSpec = spec || currentStepTypeSpec
        var expectedCategories = normalizeTokenList(ruleSpec && ruleSpec.categories)
        var expectedActions = normalizeTokenList(ruleSpec && ruleSpec.actions)

        if (expectedCategories.length === 0 && expectedActions.length === 0) {
            return true
        }

        var categoryKey = String((item && item.category) || "").trim().toLowerCase()
        var categoryMatched = expectedCategories.length === 0 || expectedCategories.indexOf(categoryKey) >= 0
        if (!categoryMatched) {
            return false
        }

        var candidateActions = normalizeList(item && item.template_actions)
            .concat(normalizeList(item && item.recommended_actions))
        return expectedActions.length === 0 || intersectsNormalizedTokens(candidateActions, expectedActions)
    }

    function filterSuggestionsByCurrentStepType(items) {
        var list = normalizeList(items)
        var spec = currentStepTypeSpec
        if (!spec || (!normalizeTokenList(spec.categories).length && !normalizeTokenList(spec.actions).length)) {
            return list
        }
        return list.filter(function(item) {
            return itemMatchesCurrentStepType(item, spec)
        })
    }

    function updateSuggestionHintMessage() {
        var totalCount = normalizeList(suggestionItems).length
        var matchedCount = normalizeList(stepTypeMatchedSuggestionItems).length
        var strategyMatchedCount = normalizeList(strategyTypeMatchedSuggestionItems).length
        var stepLabel = String((currentStepTypeSpec && currentStepTypeSpec.label) || selectedGroupTitle || "当前步骤")
        var strategyLabel = String((currentStrategyTypeSpec && currentStrategyTypeSpec.label) || resolvedStrategyLabel())
        var marketBiasHint = String((currentStrategyTypeSpec && currentStrategyTypeSpec.marketBiasHint) || "")

        if (totalCount > 0) {
            if (strategyMatchedCount > 0 && strategyMatchedCount < totalCount) {
                hintMessage = "当前先按“" + strategyLabel + "”预过滤"
                    + (marketBiasHint !== "" ? "，优先展示“" + marketBiasHint + "”相关规则" : "")
                    + "；当前保留 " + strategyMatchedCount + " 条更贴近该策略的规则，其中与“" + stepLabel + "”直接相关的会优先排在前面。"
                return
            }
            if (strategyMatchedCount === 0 && (normalizeTokenList(currentStrategyTypeSpec && currentStrategyTypeSpec.preferredTags).length > 0
                    || normalizeTokenList(currentStrategyTypeSpec && currentStrategyTypeSpec.preferredCategories).length > 0
                    || normalizeTokenList(currentStrategyTypeSpec && currentStrategyTypeSpec.preferredKeywords).length > 0)) {
                hintMessage = "当前策略类型下没有足够强的专属规则，已回退展示同阶段通用规则，避免把结果筛空。"
                return
            }
            if (matchedCount > 0 && matchedCount < totalCount) {
                hintMessage = resolvedTermDisplayName
                    ? ("已识别术语，当前共返回 " + totalCount + " 条模板，其中 " + matchedCount + " 条与“" + stepLabel + "”直接相关，并已优先排在前面。")
                    : ("未锁定单一术语，当前共返回 " + totalCount + " 条模板，其中 " + matchedCount + " 条与“" + stepLabel + "”直接相关，并已优先排在前面。")
                return
            }
            if (matchedCount === 0 && normalizeTokenList(currentStepTypeSpec && currentStepTypeSpec.categories).length > 0) {
                hintMessage = "当前步骤没有强匹配规则，下面继续展示当前策略阶段内的相似结果，避免漏掉可用规则。"
                return
            }
            hintMessage = resolvedTermDisplayName
                ? ("已返回 " + totalCount + " 条模板建议。")
                : ("未锁定单一术语，已按相似度返回 " + totalCount + " 条模板建议。")
            return
        }

        if (resolvedTermDisplayName) {
            hintMessage = "已识别术语，但当前筛选条件下没有可展示的模板。"
        } else {
            hintMessage = "没有找到足够接近的术语，可换一种市场表达再试。"
        }
    }

    function contextualPlaceholder() {
        if (queryPlaceholderText !== "") {
            return queryPlaceholderText
        }
        if (selectedGroupTitle !== "") {
            return "围绕“" + selectedGroupTitle + "”输入术语"
        }
        if (phaseLocked) {
            return "输入" + Bridge.StrategyBridge.phaseDisplayName(phaseLockValue) + "术语"
        }
        return "输入交易术语，例如：情绪修复后午后反杀"
    }

    function selectedPhaseValue() {
        if (showInlinePhaseInputs) {
            return Bridge.StrategyBridge.normalizePhaseKey(inlineActivePhase)
        }
        if (phaseLocked) {
            return Bridge.StrategyBridge.normalizePhaseKey(phaseLockValue)
        }
        var option = phaseOptions[phaseCombo.currentIndex]
        var optionValue = option ? option.value : ""
        if (optionValue !== "") {
            return optionValue
        }
        return contextualPhaseValue
    }

    function phaseDraftValue(phase) {
        var key = Bridge.StrategyBridge.normalizePhaseKey(phase)
        if (!key) {
            return ""
        }
        return String((inlinePhaseDrafts || ({}))[key] || "")
    }

    function setPhaseDraftValue(phase, text) {
        var key = Bridge.StrategyBridge.normalizePhaseKey(phase)
        if (!key) {
            return
        }
        var nextDrafts = ({})
        var drafts = inlinePhaseDrafts || ({})
        for (var draftKey in drafts) {
            nextDrafts[draftKey] = drafts[draftKey]
        }
        nextDrafts[key] = String(text || "")
        inlinePhaseDrafts = nextDrafts
    }

    function currentStrategyHint() {
        if (selectedGroupTitle !== "") {
            var stageLabel = selectedStageTitle !== "" ? selectedStageTitle : Bridge.StrategyBridge.phaseDisplayName(phaseLockValue)
            return "当前正在为“" + stageLabel + " / " + selectedGroupTitle + "”补规则，建议会优先按“" + roleDisplayName(selectedGroupRole) + "”角色和当前阶段自动过滤。"
        }
        if (phaseLocked) {
            var phaseKey = Bridge.StrategyBridge.normalizePhaseKey(phaseLockValue)
            if (phaseKey === "signal") {
                return "在这里输入入场或观察信号术语，应用后只会写入信号阶段。"
            }
            if (phaseKey === "rebalance") {
                return "在这里输入持仓管理或退出术语，应用后只会写入退出阶段。"
            }
            if (phaseKey === "market") {
                return "在这里输入市场过滤或风控术语，应用后只会写入市场风控阶段。"
            }
        }

        var behaviorKind = currentStrategyBehaviorKind
        var strategyTypeIndex = Bridge.StrategyBridge.normalizeStrategyTypeIndex(selectedStrategyTypeIndex)
        if (behaviorKind === 0
                && strategyTypeIndex === 1) {
            return "趋势突破策略建议绑定突破确认类入场模板，并搭配趋势衰减退出与市场风控模板。"
        }
        if (behaviorKind === 0) {
            return "趋势策略建议至少分别绑定一条入场/观察信号和一条持仓管理/退出模板；市场/风控模板按需补充。"
        }
        if (behaviorKind === 1) {
            return "均值回归策略更适合绑定回归入场与失败退出模板，市场风控通常只做过滤。"
        }
        if (behaviorKind === 2) {
            return "动量策略通常需要入场信号和趋势衰减退出，市场风控用于过滤退潮时段。"
        }
        if (behaviorKind === 3
                || behaviorKind === 4
                || behaviorKind === 5) {
            return "组合与模型类策略通常优先补持仓管理和市场风控模板，入场确认更多依赖评分与池内排序。"
        }
        if (behaviorKind === 6) {
            return "事件驱动策略建议优先绑定事件确认入场和事件失效退出模板，再补市场风控。"
        }
        if (behaviorKind === 7) {
            return "高频策略建议优先绑定微结构入场与执行约束模板，市场风控更多负责交易时段和流动性限制。"
        }
        return "可以同时为不同阶段绑定模板；同一阶段再次应用会替换该阶段当前模板。"
    }

    function suggestionHighlightTags(item) {
        var tags = []
        var seen = ({})

        function appendTag(value, tone) {
            var text = String(value || "").trim()
            if (!text || seen[text]) {
                return
            }
            seen[text] = true
            tags.push({ label: text, tone: tone || "muted" })
        }

        appendTag(item && (item.term_display_name || item.termDisplayName), "info")

        var aliases = normalizeList(item && item.matched_aliases)
        for (var aliasIndex = 0; aliasIndex < aliases.length; ++aliasIndex) {
            appendTag(aliases[aliasIndex], "muted")
        }

        var actions = normalizeList(item && item.recommended_actions)
        for (var actionIndex = 0; actionIndex < actions.length; ++actionIndex) {
            appendTag(actions[actionIndex], "accent")
        }

        return tags
    }

    function normalizeList(value) {
        return Array.isArray(value) ? value : []
    }

    function shouldShowPhaseExamples(phaseValue) {
        if (!compactInlineLayout) {
            return true
        }
        return Bridge.StrategyBridge.normalizePhaseKey(phaseValue) === Bridge.StrategyBridge.normalizePhaseKey(inlineActivePhase)
    }

    function visiblePhaseExamples(panelSpec) {
        var examples = normalizeList(panelSpec && panelSpec.examples)
        if (!compactInlineLayout) {
            return examples
        }
        if (!shouldShowPhaseExamples(panelSpec && panelSpec.value)) {
            return []
        }
        return examples
    }

    function syncSuggestedPhaseSelection() {
        if (!phaseCombo || phaseLocked || contextualPhaseValue === "") {
            return
        }
        for (var index = 0; index < phaseOptions.length; ++index) {
            if (phaseOptions[index].value === contextualPhaseValue) {
                phaseCombo.currentIndex = index
                return
            }
        }
    }

    function missingFeatureCount(item) {
        return normalizeList(item && item.missing_feature_labels).length
    }

    function matchedAliasCount(item) {
        return normalizeList(item && item.matched_aliases).length
    }

    function isReadySuggestion(item) {
        return !!(item && item.is_ready)
    }

    function suggestionUpdatedTimestamp(item) {
        var rawValue = String((item && (item.file_modified_at || item.fileModifiedAt)) || "").trim()
        if (!rawValue) {
            return 0
        }
        var parsedValue = Date.parse(rawValue)
        return isNaN(parsedValue) ? 0 : parsedValue
    }

    function countReadySuggestions(items) {
        var list = normalizeList(items)
        var count = 0
        for (var index = 0; index < list.length; ++index) {
            if (isReadySuggestion(list[index])) {
                count += 1
            }
        }
        return count
    }

    function sortSuggestionItems(items) {
        var list = normalizeList(items).slice()
        list.sort(function(left, right) {
            var leftStepMatch = itemMatchesCurrentStepType(left) ? 1 : 0
            var rightStepMatch = itemMatchesCurrentStepType(right) ? 1 : 0
            if (leftStepMatch !== rightStepMatch) {
                return rightStepMatch - leftStepMatch
            }

            var leftStrategyScore = itemStrategyTypeSortScore(left)
            var rightStrategyScore = itemStrategyTypeSortScore(right)
            if (leftStrategyScore !== rightStrategyScore) {
                return rightStrategyScore - leftStrategyScore
            }

            var leftReady = isReadySuggestion(left) ? 1 : 0
            var rightReady = isReadySuggestion(right) ? 1 : 0
            if (leftReady !== rightReady) {
                return rightReady - leftReady
            }

            var leftTimestamp = suggestionUpdatedTimestamp(left)
            var rightTimestamp = suggestionUpdatedTimestamp(right)
            if (leftTimestamp !== rightTimestamp) {
                return rightTimestamp - leftTimestamp
            }

            var leftScore = Number(left && left.score) || 0
            var rightScore = Number(right && right.score) || 0
            if (leftScore !== rightScore) {
                return rightScore - leftScore
            }

            var missingCountDiff = missingFeatureCount(left) - missingFeatureCount(right)
            if (missingCountDiff !== 0) {
                return missingCountDiff
            }

            var aliasCountDiff = matchedAliasCount(right) - matchedAliasCount(left)
            if (aliasCountDiff !== 0) {
                return aliasCountDiff
            }

            var leftDefault = left && left.is_default_template ? 1 : 0
            var rightDefault = right && right.is_default_template ? 1 : 0
            if (leftDefault !== rightDefault) {
                return rightDefault - leftDefault
            }

            var leftName = String((left && (left.template_display_name || left.template_id)) || "")
            var rightName = String((right && (right.template_display_name || right.template_id)) || "")
            return leftName.localeCompare(rightName, "zh-CN")
        })

        return list
    }

    function groupSuggestionItems(items) {
        var list = normalizeList(items)
        var sectionMap = ({})
        var sections = []

        for (var index = 0; index < list.length; ++index) {
            var item = list[index]
            var phaseKey = Bridge.StrategyBridge.normalizePhaseKey(item && item.phase) || "other"
            var categoryKey = String((item && item.category) || "").trim().toLowerCase()
            var sectionKey = phaseKey + "|" + categoryKey
            var section = sectionMap[sectionKey]
            if (!section) {
                section = {
                    id: sectionKey,
                    title: Bridge.StrategyBridge.phaseDisplayName(item && item.phase),
                    subtitle: Bridge.StrategyBridge.strategyTypeName(item && item.category),
                    items: [],
                    readyCount: 0
                }
                sectionMap[sectionKey] = section
                sections.push(section)
            }

            section.items.push(item)
            if (isReadySuggestion(item)) {
                section.readyCount += 1
            }
        }

        return sections
    }

    function applyQuickQuery(query, phaseValue) {
        if (showInlinePhaseInputs) {
            var inlinePhase = Bridge.StrategyBridge.normalizePhaseKey(phaseValue)
            if (inlinePhase !== "") {
                inlineActivePhase = inlinePhase
                setPhaseDraftValue(inlinePhase, query)
                submitQuery(query, inlinePhase)
                return
            }
        }

        queryField.text = query
        if (phaseValue === undefined || phaseValue === null) {
            return
        }
        for (var i = 0; i < phaseOptions.length; ++i) {
            if (phaseOptions[i].value === phaseValue) {
                phaseCombo.currentIndex = i
                break
            }
        }
        submitQuery()
    }

    function showApplyFeedback(suggestion, applyMode) {
        var templateName = String(
            (suggestion && (suggestion.template_display_name || suggestion.templateDisplayName || suggestion.template_id)) || "该模板"
        ).trim()
        var phaseLabel = Bridge.StrategyBridge.phaseDisplayName(suggestion && suggestion.phase)
        applyFeedbackTone = "success"
        if (selectedGroupTitle !== "") {
            applyFeedbackMessage = "已将 “" + templateName + "” 加入“" + selectedGroupTitle + "”，并顺带补充策略描述与标签。"
        } else {
            applyFeedbackMessage = "已将 “" + templateName + "” 应用到“" + phaseLabel + "”阶段，并顺带补充策略描述与标签。"
        }
        applyFeedbackTimer.restart()
    }

    function submitQuery(overrideQueryText, overridePhase) {
        var queryText = String(overrideQueryText !== undefined
            ? overrideQueryText
            : (showInlinePhaseInputs ? phaseDraftValue(inlineActivePhase) : queryField.text)).trim()
        var phaseValue = Bridge.StrategyBridge.normalizePhaseKey(overridePhase)
        if (phaseValue === "") {
            phaseValue = selectedPhaseValue()
        }
        if (showInlinePhaseInputs && phaseValue !== "") {
            inlineActivePhase = phaseValue
        }
        if (!queryText) {
            errorMessage = "请输入要匹配的交易术语"
            hintMessage = "例如：情绪修复后午后反杀、高位炸板次日缩量阴跌。"
            hasSearched = false
            suggestionItems = []
            resolvedTermId = ""
            resolvedTermDisplayName = ""
            return
        }

        if (!ruleTemplateSuggestionService) {
            errorMessage = "RuleTemplateSuggestionService 未接入"
            hintMessage = "当前无法请求规则模板建议。"
            return
        }

        if (typeof ruleTemplateSuggestionService.initialize === "function") {
            ruleTemplateSuggestionService.initialize()
        }

        hasSearched = true
        errorMessage = ""
        suggestionItems = []
        resolvedTermId = ""
        resolvedTermDisplayName = ""
        hintMessage = "正在查询规则模板建议..."
        // 先生成请求 ID 并赋值 activeRequestId, 确保 C++ 同步信号到达时已可匹配
        var reqId = "req_" + Date.now() + "_" + Math.floor(Math.random() * 10000)
        activeRequestId = reqId
        ruleTemplateSuggestionService.suggestTemplatesRequestAsync(reqId, {
            text: queryText,
            phase: phaseValue,
            action: root.contextualActionFilter(),
            stageId: root.selectedStageId,
            groupRole: root.selectedGroupRole,
            groupId: root.selectedGroupId,
            strategyProfile: root.strategyProfile,
            onlyReady: readySwitch.checked,
            limit: 72
        })
    }

    radius: 10
    color: "#1e293b"
    border.width: 1
    border.color: "#334155"

    Component.onCompleted: {
        if (ruleTemplateSuggestionService && typeof ruleTemplateSuggestionService.initialize === "function") {
            ruleTemplateSuggestionService.initialize()
        }
        syncSuggestedPhaseSelection()
    }

    onSelectedStageIdChanged: {
        syncSuggestedPhaseSelection()
        if (hasSearched && !requestInFlight && !errorMessage) {
            updateSuggestionHintMessage()
        }
    }

    onSelectedGroupIdChanged: {
        if (hasSearched && !requestInFlight && !errorMessage) {
            updateSuggestionHintMessage()
        }
    }

    onSelectedGroupRoleChanged: {
        if (hasSearched && !requestInFlight && !errorMessage) {
            updateSuggestionHintMessage()
        }
    }

    Connections {
        target: root.ruleTemplateSuggestionService

        function onSuggestionReady(result) {
            if (!result || result.requestId !== root.activeRequestId) {
                return
            }

            root.activeRequestId = ""
            root.errorMessage = ""
            root.resolvedTermId = result.resolvedTermId || ""
            root.resolvedTermDisplayName = result.resolvedTermDisplayName || ""
            root.suggestionItems = root.normalizeList(result.suggestions)
            root.updateSuggestionHintMessage()
        }

        function onSuggestionFailed(error) {
            if (!error || error.requestId !== root.activeRequestId) {
                return
            }

            root.activeRequestId = ""
            root.suggestionItems = []
            root.resolvedTermId = ""
            root.resolvedTermDisplayName = ""
            root.errorMessage = error.error || "规则模板建议请求失败"
            root.hintMessage = "可以调整术语描述后重新查询。"
        }
    }

    Timer {
        id: applyFeedbackTimer
        interval: 2600
        repeat: false
        onTriggered: root.applyFeedbackMessage = ""
    }

    Component {
        id: suggestionCardDelegate

        Rectangle {
            property var insight: Bridge.StrategyBridge.getTemplateInsight(modelData)
            property bool secondaryCollapsible: Bridge.StrategyBridge.normalizePhaseKey(modelData.phase) === "market"
            property bool secondaryExpanded: !secondaryCollapsible
            property bool hasExtraDetails: root.normalizeList(modelData.matched_aliases).length > 0
                || root.normalizeList(modelData.missing_feature_labels).length > 0
                || root.normalizeList(modelData.recommended_actions).length > 0
                || !!(modelData.file_name)
                || !!(modelData.file_modified_at || modelData.fileModifiedAt)
            property bool detailsExpanded: false
            Layout.fillWidth: true
            radius: 10
            color: "#1a2332"
            border.width: 1
            border.color: modelData.is_ready ? "#0f766e" : "#475569"
            implicitHeight: suggestionColumn.implicitHeight + 20

            ColumnLayout {
                id: suggestionColumn
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: modelData.template_display_name || modelData.template_id || "未命名模板"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: "#f8fafc"
                        wrapMode: Text.WordWrap
                    }

                    Rectangle {
                        radius: 10
                        color: modelData.is_ready ? "#064e3b" : "#3f3f46"
                        border.width: 1
                        border.color: modelData.is_ready ? "#10b981" : "#71717a"
                        implicitWidth: readinessText.implicitWidth + 14
                        implicitHeight: 22

                        Text {
                            id: readinessText
                            anchors.centerIn: parent
                            text: modelData.is_ready ? "已就绪" : "缺少特征"
                            font.pixelSize: 11
                            font.weight: Font.Medium
                            color: modelData.is_ready ? "#d1fae5" : "#f4f4f5"
                        }
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: root.suggestionHighlightTags(modelData)

                        delegate: Rectangle {
                            required property var modelData
                            visible: !!modelData.label
                            radius: 10
                            color: modelData.tone === "accent" ? "#102a43"
                                : modelData.tone === "info" ? "#172554"
                                : "#1e293b"
                            border.width: 1
                            border.color: modelData.tone === "accent" ? "#0369a1"
                                : modelData.tone === "info" ? "#2563eb"
                                : "#334155"
                            implicitWidth: tagLabel.implicitWidth + 14
                            implicitHeight: 24

                            Text {
                                id: tagLabel
                                anchors.centerIn: parent
                                text: modelData.label
                                font.pixelSize: 11
                                color: modelData.tone === "accent" ? "#bae6fd"
                                    : modelData.tone === "info" ? "#dbeafe"
                                    : "#cbd5e1"
                            }
                        }
                    }

                    Repeater {
                        model: [
                            Bridge.StrategyBridge.phaseDisplayName(modelData.phase),
                            Bridge.StrategyBridge.strategyTypeName(modelData.category),
                            modelData.is_default_template ? "默认模板" : ""
                        ]

                        delegate: Rectangle {
                            visible: modelData !== ""
                            radius: 10
                            color: "#1e293b"
                            border.width: 1
                            border.color: "#334155"
                            implicitWidth: badgeText.implicitWidth + 12
                            implicitHeight: 22

                            Text {
                                id: badgeText
                                anchors.centerIn: parent
                                text: modelData
                                font.pixelSize: 11
                                color: "#cbd5e1"
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: insight === null
                    text: modelData.summary || "模板未附带摘要说明。"
                    font.pixelSize: 12
                    color: "#cbd5e1"
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    visible: insight !== null
                    radius: 8
                    color: "#0b1220"
                    border.width: 1
                    border.color: "#334155"
                    implicitHeight: marketInsightColumn.implicitHeight + 18

                    ColumnLayout {
                        id: marketInsightColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: Bridge.StrategyBridge.insightSectionTitle(modelData.phase, false)
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            color: "#f8fafc"
                        }

                        Text {
                            Layout.fillWidth: true
                            text: insight && insight.summary ? insight.summary : (modelData.summary || "")
                            font.pixelSize: 11
                            color: "#cbd5e1"
                            wrapMode: Text.WordWrap
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 6
                            color: "#1f2937"
                            border.width: 1
                            border.color: "#7c2d12"
                            implicitHeight: freezeColumn.implicitHeight + 16

                            ColumnLayout {
                                id: freezeColumn
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 4

                                Text {
                                    Layout.fillWidth: true
                                    text: Bridge.StrategyBridge.insightPrimaryTitle(insight)
                                    font.pixelSize: 11
                                    font.weight: Font.Medium
                                    color: "#fdba74"
                                }

                                Repeater {
                                    model: Bridge.StrategyBridge.insightPrimaryItems(insight)

                                    delegate: Text {
                                        Layout.fillWidth: true
                                        text: (index + 1) + ". " + modelData
                                        font.pixelSize: 11
                                        color: "#e5e7eb"
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 6
                            color: "#102a43"
                            border.width: 1
                            border.color: "#0369a1"
                            implicitHeight: repairColumn.implicitHeight + 16

                            ColumnLayout {
                                id: repairColumn
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 6

                                Item {
                                    Layout.fillWidth: true
                                    implicitHeight: repairHeader.implicitHeight

                                    RowLayout {
                                        id: repairHeader
                                        anchors.fill: parent
                                        spacing: 8

                                        Text {
                                            Layout.fillWidth: true
                                            text: Bridge.StrategyBridge.insightSecondaryTitle(insight)
                                            font.pixelSize: 11
                                            font.weight: Font.Medium
                                            color: "#7dd3fc"
                                        }

                                        Text {
                                            visible: secondaryCollapsible
                                            text: secondaryExpanded ? "收起" : "展开"
                                            font.pixelSize: 10
                                            font.weight: Font.Medium
                                            color: "#bae6fd"
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: secondaryCollapsible
                                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: secondaryExpanded = !secondaryExpanded
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    visible: !secondaryCollapsible || secondaryExpanded
                                    spacing: 4

                                    Repeater {
                                        model: Bridge.StrategyBridge.insightSecondaryItems(insight)

                                        delegate: Text {
                                            Layout.fillWidth: true
                                            text: (index + 1) + ". " + modelData
                                            font.pixelSize: 11
                                            color: "#e0f2fe"
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 6
                    color: "#0b1220"
                    border.width: 1
                    border.color: "#23324a"
                    implicitHeight: detailHeaderRow.implicitHeight + 14

                    RowLayout {
                        id: detailHeaderRow
                        anchors.fill: parent
                        anchors.margins: 7
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: root.selectedGroupTitle !== ""
                                ? ("应用效果: 会直接加入当前规则组“" + root.selectedGroupTitle + "”；策略描述和标签会一并补充，标签只用于后续检索和概览。")
                                : "应用效果: 会写入当前建议对应阶段；策略描述和标签会一并补充，标签只用于后续检索和概览。"
                            font.pixelSize: 11
                            color: "#93c5fd"
                            elide: Text.ElideRight
                        }

                        Text {
                            visible: hasExtraDetails
                            text: detailsExpanded ? "收起细节" : "更多细节"
                            font.pixelSize: 10
                            font.weight: Font.Medium
                            color: "#cbd5e1"
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: hasExtraDetails
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: detailsExpanded = !detailsExpanded
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    visible: hasExtraDetails && detailsExpanded
                    radius: 6
                    color: "#0b1220"
                    border.width: 1
                    border.color: "#23324a"
                    implicitHeight: detailColumn.implicitHeight + 16

                    ColumnLayout {
                        id: detailColumn
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 5

                        Text {
                            Layout.fillWidth: true
                            visible: !!(modelData.file_modified_at || modelData.fileModifiedAt)
                            text: "最近更新: " + String(modelData.file_modified_at || modelData.fileModifiedAt || "")
                            font.pixelSize: 11
                            color: "#93c5fd"
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: root.normalizeList(modelData.matched_aliases).length > 0
                            text: "命中别名: " + root.normalizeList(modelData.matched_aliases).join(" / ")
                            font.pixelSize: 11
                            color: "#7dd3fc"
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: root.normalizeList(modelData.missing_feature_labels).length > 0
                            text: "缺失特征: " + root.normalizeList(modelData.missing_feature_labels).join("、")
                            font.pixelSize: 11
                            color: "#fca5a5"
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: root.normalizeList(modelData.recommended_actions).length > 0
                            text: "建议动作: " + root.normalizeList(modelData.recommended_actions).join(" / ")
                            font.pixelSize: 11
                            color: "#94a3b8"
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "模板文件: " + (modelData.file_name || "未知")
                            font.pixelSize: 11
                            color: "#64748b"
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: root.compactSuggestionActionRow ? 6 : 8

                    BaseComponents.ActionButton {
                        label: root.selectedGroupTitle !== "" ? "加入当前规则组" : "应用到当前阶段"
                        tone: modelData.is_ready ? "success" : "primary"
                        buttonHeight: 32
                        labelSize: 12
                        onClicked: {
                            root.applySuggestionRequested(modelData, "all")
                            root.showApplyFeedback(modelData, "all")
                        }
                    }

                    Text {
                        width: Math.max(120, parent ? parent.width - 12 : 120)
                        text: "会同步补充描述和标签；标签仅用于策略库检索与概览，不影响规则执行。"
                        font.pixelSize: 11
                        color: "#94a3b8"
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    ScrollView {
        id: suggestionScrollView
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            id: suggestionPanelLayout
            width: suggestionScrollView.availableWidth
            spacing: 10

            Item {
                width: 1
                height: 2
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        text: root.panelTitle
                        font.pixelSize: 16
                        font.weight: Font.Medium
                        color: "#f1f5f9"
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        text: root.hintMessage
                        font.pixelSize: 12
                        color: root.errorMessage ? "#fca5a5" : "#94a3b8"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Text {
                        text: root.currentStrategyHint()
                        font.pixelSize: 11
                        color: "#7dd3fc"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Text {
                        visible: root.selectedGroupTitle !== ""
                        text: "当前上下文: " + (root.selectedStageTitle || "未指定阶段") + " / " + root.selectedGroupTitle + " / " + root.roleDisplayName(root.selectedGroupRole)
                        font.pixelSize: 11
                        color: "#fbbf24"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                Rectangle {
                    radius: 11
                    color: root.requestInFlight ? "#1d4ed8" : "#1e293b"
                    border.width: 1
                    border.color: root.requestInFlight ? "#60a5fa" : "#475569"
                    implicitWidth: statusText.implicitWidth + 18
                    implicitHeight: 24

                    Text {
                        id: statusText
                        anchors.centerIn: parent
                        text: root.requestInFlight ? "查询中" : "就绪"
                        font.pixelSize: 11
                        font.weight: Font.Medium
                        color: "#eff6ff"
                    }
                }
            }

            TextField {
                id: queryField
                Layout.fillWidth: true
                visible: !root.showInlinePhaseInputs
                placeholderText: root.queryPlaceholderText !== ""
                    ? root.queryPlaceholderText
                    : root.contextualPlaceholder()
                color: "#f1f5f9"
                font.pixelSize: 14
                padding: 10
                selectByMouse: true
                enabled: !root.requestInFlight
                background: Rectangle {
                    implicitHeight: 42
                    radius: 6
                    color: "#020617"
                    border.width: 1
                    border.color: root.errorMessage ? "#ef4444" : "#334155"
                }
                onAccepted: root.submitQuery()
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                visible: !root.showInlinePhaseInputs

                ColumnLayout {
                    visible: !root.phaseLocked && root.contextualPhaseValue === ""
                    spacing: 4

                    Text {
                        text: "阶段筛选"
                        font.pixelSize: 12
                        color: "#94a3b8"
                    }

                    ComboBox {
                        id: phaseCombo
                        Layout.preferredWidth: 148
                        model: root.phaseOptions
                        textRole: "label"
                        enabled: !root.requestInFlight
                        background: Rectangle {
                            implicitHeight: 36
                            radius: 6
                            color: "#020617"
                            border.width: 1
                            border.color: "#334155"
                        }
                        contentItem: Text {
                            text: phaseCombo.displayText
                            color: "#f1f5f9"
                            font.pixelSize: 12
                            padding: 8
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                ColumnLayout {
                    spacing: 4

                    Text {
                        text: "仅显示可直接绑定模板"
                        font.pixelSize: 12
                        color: "#94a3b8"
                    }

                    Switch {
                        id: readySwitch
                        checked: root.onlyReady
                        enabled: !root.requestInFlight
                        onCheckedChanged: root.onlyReady = checked
                        indicator: Rectangle {
                            implicitWidth: 36
                            implicitHeight: 20
                            radius: 10
                            color: parent.checked ? "#0f766e" : "#334155"
                            border.width: 1
                            border.color: parent.checked ? "#2dd4bf" : "#475569"

                            Rectangle {
                                x: parent.checked ? parent.width - width - 2 : 2
                                y: 2
                                width: 16
                                height: 16
                                radius: 8
                                color: "#f8fafc"
                                Behavior on x {
                                    NumberAnimation { duration: 180 }
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                BaseComponents.ActionButton {
                    label: root.requestInFlight ? "查询中" : "获取建议"
                    tone: "primary"
                    buttonWidth: 0
                    buttonEnabled: !root.requestInFlight
                    onClicked: root.submitQuery()
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: 8
                visible: !root.showInlinePhaseInputs && root.displayedQuickQueries.length > 0

                Repeater {
                    model: root.displayedQuickQueries

                    delegate: BaseComponents.ActionChip {
                        label: modelData.label
                        tone: "muted"
                        chipEnabled: !root.requestInFlight
                        useCustomColors: true
                        customBackgroundColor: "#172033"
                        customBorderColor: "#334155"
                        customTextColor: "#cbd5e1"
                        onClicked: root.applyQuickQuery(modelData.label, modelData.phase)
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: root.showInlinePhaseInputs

                Repeater {
                    model: root.displayedInlinePhasePanels

                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        radius: 8
                        color: root.inlineActivePhase === modelData.value ? "#111827" : "#0b1220"
                        border.width: 1
                        border.color: root.inlineActivePhase === modelData.value ? "#2563eb" : "#334155"
                        implicitHeight: phaseRowLayout.implicitHeight + 16

                        ColumnLayout {
                            id: phaseRowLayout
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: root.compactInlineLayout ? 6 : 8

                                Rectangle {
                                    radius: 10
                                    color: "#172554"
                                    border.width: 1
                                    border.color: "#2563eb"
                                    implicitWidth: phaseLabelText.implicitWidth + (root.compactInlineLayout ? 10 : 14)
                                    implicitHeight: 24

                                    Text {
                                        id: phaseLabelText
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        font.pixelSize: 11
                                        font.weight: Font.Medium
                                        color: "#dbeafe"
                                    }
                                }

                                TextField {
                                    Layout.fillWidth: true
                                    placeholderText: modelData.placeholder
                                    text: root.phaseDraftValue(modelData.value)
                                    enabled: !root.requestInFlight
                                    color: "#f1f5f9"
                                    font.pixelSize: 13
                                    padding: 10
                                    selectByMouse: true
                                    background: Rectangle {
                                        implicitHeight: 40
                                        radius: 6
                                        color: "#020617"
                                        border.width: 1
                                        border.color: root.inlineActivePhase === modelData.value ? "#2563eb" : "#334155"
                                    }
                                    onTextChanged: {
                                        root.inlineActivePhase = modelData.value
                                        root.setPhaseDraftValue(modelData.value, text)
                                    }
                                    onActiveFocusChanged: {
                                        if (activeFocus) {
                                            root.inlineActivePhase = modelData.value
                                        }
                                    }
                                    onAccepted: {
                                        root.inlineActivePhase = modelData.value
                                        root.submitQuery(text, modelData.value)
                                    }
                                }

                                BaseComponents.ActionButton {
                                    label: root.requestInFlight && root.inlineActivePhase === modelData.value ? "查询中" : "查询"
                                    tone: "primary"
                                    buttonWidth: 0
                                    buttonHeight: 32
                                    labelSize: 12
                                    buttonEnabled: !root.requestInFlight
                                    onClicked: {
                                        root.inlineActivePhase = modelData.value
                                        root.submitQuery(root.phaseDraftValue(modelData.value), modelData.value)
                                    }
                                }
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                visible: root.visiblePhaseExamples(modelData).length > 0

                                Repeater {
                                    model: root.visiblePhaseExamples(modelData)

                                    delegate: BaseComponents.ActionChip {
                                        label: modelData
                                        tone: "muted"
                                        chipEnabled: !root.requestInFlight
                                        useCustomColors: true
                                        customBackgroundColor: "#172033"
                                        customBorderColor: "#334155"
                                        customTextColor: "#cbd5e1"
                                        onClicked: root.applyQuickQuery(modelData, phaseRowLayout.parent.modelData.value)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: root.hasSearched
                radius: 8
                color: root.errorMessage ? "#3f1d24" : "#172033"
                border.width: 1
                border.color: root.errorMessage ? "#7f1d1d" : "#334155"
                implicitHeight: summaryColumn.implicitHeight + 18

                ColumnLayout {
                    id: summaryColumn
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        text: root.errorMessage
                            ? ("请求失败: " + root.errorMessage)
                            : (root.resolvedTermDisplayName
                                ? ("识别术语: " + root.resolvedTermDisplayName + (root.resolvedTermId ? " (" + root.resolvedTermId + ")" : ""))
                                : "未识别到明确术语，当前结果按相似度排序。")
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        color: root.errorMessage ? "#fecaca" : "#dbeafe"
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !root.errorMessage
                        text: root.surfacedSuggestionItems.length > 0
                            ? (root.readySuggestionCount > 0
                                ? ("已优先透出 " + root.readySuggestionCount + " 条可直接绑定规则，并按阶段/类别分卡片展示；同类规则再按最近更新和相关性排序。")
                                : ("返回 " + root.surfacedSuggestionItems.length + " 条建议，已按阶段/类别分卡片展示。"))
                            : "当前没有返回建议，可调整术语、阶段或关闭“仅显示可直接绑定模板”。"
                        font.pixelSize: 11
                        color: "#94a3b8"
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: root.readySuggestionCount > 0 && root.surfacedSuggestionItems.length > 0
                radius: 8
                color: "#083344"
                border.width: 1
                border.color: "#14b8a6"
                implicitHeight: surfacedText.implicitHeight + 18

                Text {
                    id: surfacedText
                    anchors.fill: parent
                    anchors.margins: 10
                    text: "优先透出 " + root.readySuggestionCount + " 条可直接绑定规则；若模板近期有更新，会在同类卡片前部优先显示。"
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    color: "#ccfbf1"
                    wrapMode: Text.WordWrap
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: root.applyFeedbackMessage !== ""
                radius: 8
                color: root.applyFeedbackTone === "secondary" ? "#1e293b"
                    : root.applyFeedbackTone === "muted" ? "#172033"
                    : "#083344"
                border.width: 1
                border.color: root.applyFeedbackTone === "secondary" ? "#3b82f6"
                    : root.applyFeedbackTone === "muted" ? "#475569"
                    : "#14b8a6"
                implicitHeight: feedbackText.implicitHeight + 18

                Text {
                    id: feedbackText
                    anchors.fill: parent
                    anchors.margins: 10
                    text: root.applyFeedbackMessage
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    color: "#e2e8f0"
                    wrapMode: Text.WordWrap
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 12
                visible: root.groupedSuggestionSections.length > 0

                Repeater {
                    model: root.groupedSuggestionSections

                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        radius: 10
                        color: "#1e293b"
                        border.width: 1
                        border.color: modelData.readyCount > 0 ? "#0f766e" : "#334155"
                        implicitHeight: sectionColumn.implicitHeight + 20

                        ColumnLayout {
                            id: sectionColumn
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.title
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                    color: "#f8fafc"
                                    elide: Text.ElideRight
                                }

                                Rectangle {
                                    visible: modelData.readyCount > 0
                                    radius: 10
                                    color: "#064e3b"
                                    border.width: 1
                                    border.color: "#10b981"
                                    implicitWidth: readyCountText.implicitWidth + 14
                                    implicitHeight: 22

                                    Text {
                                        id: readyCountText
                                        anchors.centerIn: parent
                                        text: modelData.readyCount + " 条可直绑"
                                        font.pixelSize: 11
                                        font.weight: Font.Medium
                                        color: "#d1fae5"
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: modelData.subtitle + " · 共 " + modelData.items.length + " 条"
                                font.pixelSize: 12
                                color: "#94a3b8"
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Repeater {
                                    model: modelData.items
                                    delegate: suggestionCardDelegate
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}